"""HP 4145A remote controller using ugpibd's Prologix TCP interface.

Built for an NI GPIB-USB-HS on an Apple Silicon Mac.

Requires ugpibd 0.9.0 (or compatible) started with --enable-prologix.
The source-configuration interface intentionally enforces conservative
resistor-test limits and is disabled unless allow_configuration=True.
Measurement triggering additionally requires confirmation and a configuration
applied during this program session. This is a safety guard, not a substitute
for checking the fixture/interlocks/wiring with a qualified supervisor.
"""

from __future__ import annotations

import csv
from dataclasses import dataclass
from math import isfinite
from pathlib import Path
import re
import socket
import time
from typing import Callable, Iterable


class HP4145AError(RuntimeError):
    """Communication, instrument data, or safety error."""


@dataclass(frozen=True)
class Reading:
    status: str
    value: float

    @property
    def valid(self) -> bool:
        return self.status == "N" and isfinite(self.value)


_STATUS = {
    "N": "Normal",
    "L": "Interval too short",
    "V": "A/D converter saturation",
    "X": "Oscillation",
    "C": "This channel reached compliance",
    "T": "Another channel reached compliance",
}
_READING = re.compile(
    r"^\s*([NLVXCT])\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[Ee][+-]?\d+)?)\s*$"
)
_NAME = re.compile(r"^[A-Za-z][A-Za-z0-9_]{0,5}$")


def parse_data(raw: bytes | str) -> list[Reading]:
    """Parse 4145A System Mode output, e.g. 'C 995.93E-06,N+1E-03,'."""
    if isinstance(raw, bytes):
        try:
            raw = raw.decode("ascii")
        except UnicodeDecodeError as exc:
            raise HP4145AError("Instrument returned non-ASCII data") from exc
    # COMMA delimiter is configured on this user's 4145A.
    tokens = [part.strip() for part in raw.replace("\r", "").replace("\n", ",").split(",")]
    tokens = [part for part in tokens if part]
    if not tokens:
        raise HP4145AError("Empty trace; check measurement availability and instrument name")
    results: list[Reading] = []
    for index, token in enumerate(tokens):
        match = _READING.fullmatch(token)
        if not match:
            raise HP4145AError(f"Unrecognized sample #{index + 1}: {token!r}")
        status, number = match.groups()
        value = float(number)
        if not isfinite(value):
            raise HP4145AError(f"Non-finite sample #{index + 1}")
        results.append(Reading(status, value))
    return results


class HP4145A:
    """One-at-a-time Prologix session. Do not use NIUSBGPIB concurrently."""

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 1234,
        address: int = 16,
        timeout: float = 6.0,
        command_delay: float = 0.10,
        allow_configuration: bool = False,
        socket_factory: Callable[..., socket.socket] = socket.create_connection,
    ) -> None:
        if not 0 <= address <= 30:
            raise ValueError("Primary GPIB address must be from 0 to 30")
        if not 0 <= command_delay <= 1.0:
            raise ValueError("command_delay must be between 0 and 1 second")
        self.host, self.port, self.address = host, port, address
        self.timeout = timeout
        self.command_delay = command_delay
        self.allow_configuration = allow_configuration
        self._socket_factory = socket_factory
        self._sock: socket.socket | None = None
        self._channels_applied = False
        self._sweep_applied = False
        self._last_sweep_points = None

    def __enter__(self) -> "HP4145A":
        try:
            self._sock = self._socket_factory((self.host, self.port), timeout=self.timeout)
            self._sock.settimeout(self.timeout)
            for command in (
                "++mode 1", f"++addr {self.address}", "++auto 0",
                "++eoi 1", "++eos 2", "++eot_enable 1",
                "++eot_char 10", "++read_tmo_ms 3000",
            ):
                self._send(command)
        except (OSError, HP4145AError) as exc:
            self.close()
            raise HP4145AError(
                "Could not initialize Prologix connection. Start 'ugpibd --enable-prologix' "
                "and confirm port 1234 is listening."
            ) from exc
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def close(self) -> None:
        if self._sock is not None:
            self._sock.close()
            self._sock = None

    def _send(self, command: str) -> None:
        if self._sock is None:
            raise HP4145AError("Not connected: use 'with HP4145A() as analyzer:'")
        if "\r" in command or "\n" in command or not command.isascii():
            raise ValueError("GPIB command must be one ASCII line")
        try:
            self._sock.sendall((command + "\n").encode("ascii"))
            # This legacy HP-IB instrument and the USB bridge can need
            # a brief pause between Prologix commands. Match the known
            # working raw-socket data-transfer test.
            if self.command_delay:
                time.sleep(self.command_delay)
        except OSError as exc:
            raise HP4145AError(f"Failed to send {command!r}: {exc}") from exc

    def _recv_line(self, max_bytes: int = 64) -> bytes:
        data = bytearray()
        while len(data) < max_bytes:
            try:
                b = self._sock.recv(1)  # type: ignore[union-attr]
            except socket.timeout as exc:
                raise HP4145AError("Timed out waiting for Prologix status response") from exc
            if not b:
                raise HP4145AError("Prologix connection closed")
            data.extend(b)
            if b == b"\n":
                return bytes(data)
        raise HP4145AError("Unexpectedly long Prologix status response")

    def serial_poll(self) -> int:
        """Poll instrument status; 4145A's bit 0 (mask 0x01) = data ready."""
        self._send(f"++spoll {self.address}")
        line = self._recv_line().strip()
        try:
            status = int(line)
        except ValueError as exc:
            raise HP4145AError(f"Invalid serial-poll response: {line!r}") from exc
        if not 0 <= status <= 255:
            raise HP4145AError(f"Status byte out of range: {status}")
        return status

    def wait_data_ready(self, timeout: float = 60.0, interval: float = 0.5) -> int:
        """Wait for 4145A Data Ready (status mask 0x01), or fail on error bits."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            code = self.serial_poll()
            if code & 0x80:
                raise HP4145AError(f"Analyzer emergency status: 0x{code:02X}")
            if code & 0x0A:  # syntax error (0x02) / illegal program (0x08)
                raise HP4145AError(f"Analyzer rejected a remote command: 0x{code:02X}")
            if code & 0x01:
                return code
            time.sleep(interval)
        raise HP4145AError("Measurement data was not ready before timeout")

    def get_data(self, variable: str = "I1", max_bytes: int = 1_000_000) -> list[Reading]:
        """Request *existing* trace via DO; never starts a measurement."""
        variable = variable.upper()
        if not _NAME.fullmatch(variable):
            raise ValueError("Variable name must be 1-6 ASCII letters/digits/underscore")
        self._send(f"DO '{variable}'")
        self._send("++read eoi")
        received = bytearray()
        while len(received) < max_bytes:
            try:
                chunk = self._sock.recv(min(8192, max_bytes - len(received)))  # type: ignore[union-attr]
            except socket.timeout as exc:
                raise HP4145AError(
                    f"Timed out reading {variable} after {len(received)} byte(s). "
                    "Check the latest ugpibd log for 'endpoint stalled' or 'no devices'."
                ) from exc
            if not chunk:
                raise HP4145AError("Connection closed during measurement read")
            received.extend(chunk)
            if b"\n" in chunk:
                # 4145A terminates data with CR LF; EOT char 10 is also enabled.
                if not received.strip():
                    raise HP4145AError(
                        f"Read of {variable} returned only a terminator "
                        f"({bytes(received)!r}), not measurements. "
                        "Check the analyzer has stored data and inspect ugpibd "
                        "for GPIB write/read errors. No new sweep was triggered."
                    )
                return parse_data(bytes(received))
        raise HP4145AError("Data response exceeded maximum permitted size")

    def read_iv(self, voltage_name: str = "V1", current_name: str = "I1") -> tuple[list[Reading], list[Reading]]:
        """Fetch both traces, with strict point-count agreement."""
        v = self.get_data(voltage_name)
        i = self.get_data(current_name)
        if len(v) != len(i):
            raise HP4145AError(
                f"Voltage/current length mismatch: {len(v)} voltage points, "
                f"{len(i)} current points. Do not pair these traces."
            )
        return v, i

    def _configuration_write(self, command: str) -> str:
        """When not explicitly armed, only return the command as a preview."""
        if self.allow_configuration:
            self._send(command)
        return command

    def configure_channels(self, source_smu: int = 1, common_smu: int = 3) -> str:
        """SMU source = V/VAR1; COM return = COM/CONST; all other channels OFF."""
        if source_smu not in (1, 2, 3, 4) or common_smu not in (1, 2, 3, 4):
            raise ValueError("Source and common SMU must be from 1 to 4")
        if source_smu == common_smu:
            raise ValueError("Source and common SMU must be different")
        parts = []
        for n in range(1, 5):
            if n == source_smu:
                parts.append(f"CH{n},'V{n}','I{n}',1,1")
            elif n == common_smu:
                parts.append(f"CH{n},'V{n}','I{n}',3,3")
            else:
                parts.append(f"CH{n}")
        parts += ["VS1", "VS2", "VM1", "VM2"]
        cmd = "DE " + ";".join(parts)
        self._channels_applied = self.allow_configuration
        return self._configuration_write(cmd)

    def configure_sweep(
        self, start: float = 0.0, stop: float = 1.0,
        step: float = 0.01, current_limit_a: float = 0.001,
    ) -> str:
        """Conservative low-voltage VAR1 sweep for initial resistor validation."""
        values = (start, stop, step, current_limit_a)
        if not all(isfinite(x) for x in values):
            raise ValueError("Sweep parameters must be finite")
        if not (0.0 <= start < stop <= 1.0):
            raise ValueError("Initial test supports only 0 to +1 V")
        if not (0 < step <= 0.1):
            raise ValueError("Initial test requires a 0 < step <= 0.1 V")
        if not (0 < current_limit_a <= 0.001):
            raise ValueError("Initial test compliance cannot exceed 1 mA")
        point_count = round((stop - start) / step) + 1
        if point_count > 201 or abs(start + (point_count - 1) * step - stop) > 1e-8:
            raise ValueError("Sweep must have <=201 points and a step that evenly reaches stop")
        cmd = f"SS VR1,{start:.9g},{stop:.9g},{step:.9g},{current_limit_a:.9g}"
        self._sweep_applied = self.allow_configuration
        self._last_sweep_points = point_count
        return self._configuration_write(cmd)

    def set_integration_time(self, mode: str = "MEDIUM") -> str:
        codes = {"SHORT": "IT1", "MEDIUM": "IT2", "LONG": "IT3"}
        try:
            command = codes[mode.upper()]
        except KeyError as exc:
            raise ValueError("mode must be SHORT, MEDIUM, or LONG") from exc
        return self._configuration_write(command)

    def configure_list_display(self, variables: Iterable[str] = ("V1", "I1")) -> str:
        names = [name.upper() for name in variables]
        if not names or len(names) > 6 or any(not _NAME.fullmatch(n) for n in names):
            raise ValueError("Provide 1-6 valid monitor names")
        command = "SM DM2;LI " + ",".join(f"'{n}'" for n in names)
        return self._configuration_write(command)

    def measure(self, *, fixture_verified: bool = False, timeout: float = 60.0) -> int:
        """Trigger SINGLE; explicit opt-in AND new validated setup required."""
        if not (self.allow_configuration and self._channels_applied and self._sweep_applied):
            raise HP4145AError("Apply channel and sweep configurations first, in this session")
        if not fixture_verified:
            raise HP4145AError("Measurement not triggered: fixture verification required")
        self._send("MD ME1")
        return self.wait_data_ready(timeout=timeout)

    @staticmethod
    def save_trace_csv(path: str | Path, variable: str, readings: list[Reading]) -> Path:
        """Export one 4145A monitor variable, preserving status flags."""
        if not _NAME.fullmatch(variable.upper()):
            raise ValueError("Invalid variable name")
        dest = Path(path)
        dest.parent.mkdir(parents=True, exist_ok=True)
        with dest.open("w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow(["index", f"{variable.upper()}_value_SI", "status", "valid"])
            for idx, reading in enumerate(readings):
                writer.writerow([idx, reading.value, reading.status, reading.valid])
        return dest

    @staticmethod
    def save_csv(
        path: str | Path,
        voltage: list[Reading],
        current: list[Reading],
        expected_points: int | None = None,
    ) -> Path:
        if len(voltage) != len(current):
            raise HP4145AError("V1/I1 point counts differ; no CSV written")
        if expected_points is not None and len(voltage) != expected_points:
            raise HP4145AError(
                f"Expected {expected_points} points but got {len(voltage)}; no CSV written"
            )
        dest = Path(path)
        dest.parent.mkdir(parents=True, exist_ok=True)
        with dest.open("w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow(["index", "V1_V", "V1_status", "I1_A", "I1_status", "valid"])
            for idx, (v, i) in enumerate(zip(voltage, current)):
                writer.writerow([idx, v.value, v.status, i.value, i.status, v.valid and i.valid])
        return dest

    @staticmethod
    def plot_iv(path: str | Path, voltage: list[Reading], current: list[Reading]) -> Path:
        """Plot points; mark compliance/error points separately from normal data."""
        if len(voltage) != len(current):
            raise HP4145AError("V1/I1 point counts differ; no plot written")
        try:
            import matplotlib.pyplot as plt
        except ImportError as exc:
            raise HP4145AError("Install matplotlib for plotting: pip install matplotlib") from exc
        valid = [(v.value, i.value * 1e6) for v, i in zip(voltage, current) if v.valid and i.valid]
        invalid = [(v.value, i.value * 1e6) for v, i in zip(voltage, current) if not (v.valid and i.valid)]
        fig, ax = plt.subplots(figsize=(7, 4.4))
        if valid:
            ax.plot([x for x, _ in valid], [y for _, y in valid], "o-", label="Normal")
        if invalid:
            ax.plot([x for x, _ in invalid], [y for _, y in invalid], "x", label="Flagged (unreliable)")
        ax.set(xlabel="V1 (V)", ylabel="I1 (µA)", title="HP 4145A I–V measurement")
        ax.grid(True, alpha=0.25)
        ax.legend()
        fig.tight_layout()
        dest = Path(path)
        dest.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(dest, dpi=180)
        plt.close(fig)
        return dest

    @staticmethod
    def status_summary(readings: list[Reading]) -> dict[str, int]:
        return {name: sum(r.status == code for r in readings) for code, name in _STATUS.items() if any(r.status == code for r in readings)}
