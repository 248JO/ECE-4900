"""Additional HP 4145A measurement profiles; keeps the established driver intact.

These methods transmit source settings only after explicit fixture verification.
Time and sweep commands follow HP 4145A manual, Figure 3-39.
"""
from __future__ import annotations
import math
import re
from hp4145a import HP4145A, HP4145AError

NAME = re.compile(r"^[A-Z][A-Z0-9_]{0,5}$")

# This one I-V profile is intentionally locked. Other measurement types retain
# their existing editable settings. All current and voltage values are SI.
IV_FIXED = {
    'v1': 'V1', 'i1': 'I1', 'v3': 'V3', 'i3': 'I3',
    'smu1_mode': 'V', 'smu1_function': 'VAR1', 'smu1_source': 0.0,
    'smu3_mode': 'COM', 'smu3_function': 'CONST', 'smu3_source': 0.0,
    'sweep_start': 0.0, 'sweep_stop': 0.5, 'sweep_step': 0.005,
    'current_limit_a': 0.1,
    'apply_axes': True,
    'axis_x': 'V1', 'axis_x_min': 0.0, 'axis_x_max': 0.1,
    'axis_y1': 'I1', 'axis_y1_min': -0.1, 'axis_y1_max': 1.0,
    'axis_y2_enabled': False,
}


def effective_config(cfg):
    """Ensure I-V transmissions always use the fixed profile, even outside the GUI."""
    if cfg['measurement'] == 'I–V Sweep':
        return {**cfg, **IV_FIXED}
    return dict(cfg)


def _finite(name, value):
    if not math.isfinite(value):
        raise ValueError(f"{name} must be finite")


def graph_commands(cfg):
    """Build display commands (separate commands for precise error reporting)."""
    if not cfg.get('apply_axes', False):
        return []
    mode = cfg['measurement']
    time_mode = mode != 'I–V Sweep'
    x = cfg['axis_x']
    y1 = cfg['axis_y1']
    y2 = cfg['axis_y2']
    valid = {cfg['v1'], cfg['i1'], cfg['v3'], cfg['i3']}
    if time_mode:
        if x != 'TIME':
            raise ValueError('A time-domain measurement must use TIME on the X axis')
    elif x not in valid:
        raise ValueError('The sweep X-axis must use a defined channel name')
    if y1 not in valid:
        raise ValueError('Y1 must use a defined channel name')
    if cfg.get('axis_y2_enabled') and (y2 not in valid):
        raise ValueError('Y2 must use a defined channel name')
    def bounds(name):
        a, b = cfg[f'axis_{name}_min'], cfg[f'axis_{name}_max']
        _finite(name+' minimum', a); _finite(name+' maximum', b)
        if not a < b:
            raise ValueError(f'{name.upper()} minimum must be below maximum')
        return f'{a:.8g},{b:.8g}'
    if time_mode:
        cmds = [f"XT {bounds('x')}"]
    else:
        cmds = [f"XN '{x}',1,{bounds('x')}"]
    cmds.append(f"YA '{y1}',1,{bounds('y1')}")
    if cfg.get('axis_y2_enabled'):
        cmds.append(f"YB '{y2}',1,{bounds('y2')}")
    return cmds


def build_commands(cfg):
    """Return a validated profile as (commands, variable_names, expected_count).

    Preview doesn't connect to hardware. SMU3 is intentionally common only;
    additional SMU3 source operation needs separately established wiring.
    """
    cfg = effective_config(cfg)
    names = [cfg[k] for k in ('v1', 'i1', 'v3', 'i3')]
    if any(not NAME.fullmatch(n) for n in names) or len(set(names)) != 4:
        raise ValueError('Four distinct 1–6 character channel names are required')
    if cfg['smu3_mode'] != 'COM' or cfg['smu3_function'] != 'CONST':
        raise ValueError('Only SMU3 COM / CONST is enabled for Run')
    if cfg['smu1_source'] != 0:
        raise ValueError('Nonzero fixed source values require a separately validated profile')
    if not 0 < cfg['current_limit_a'] <= .1:
        raise ValueError('Current compliance must be between 0 and 100 mA')
    if not 0 < cfg['voltage_limit_v'] <= 1:
        raise ValueError('Voltage compliance must be between 0 and 1 V for this initial voltage test')
    mode = cfg['measurement']
    if mode == 'Current vs Time':
        source_mode, function, record = ('V','CONST',cfg['i1'])
    elif mode == 'Voltage vs Time':
        source_mode, function, record = ('I','CONST',cfg['v1'])
    elif mode == 'I–V Sweep':
        source_mode, function, record = ('V','VAR1',(cfg['v1'],cfg['i1']))
    else:
        raise ValueError('Unsupported measurement')
    if cfg['smu1_mode'] != source_mode or cfg['smu1_function'] != function:
        raise ValueError(f'{mode} needs SMU1 {source_mode} / {function}')
    ch_code = {'V':1, 'I':2}[source_mode]
    f_code = {'CONST':3, 'VAR1':1}[function]
    channels = (f"DE CH1,'{cfg['v1']}','{cfg['i1']}',{ch_code},{f_code};"
                f"CH2;CH3,'{cfg['v3']}','{cfg['i3']}',3,3;CH4;VS1;VS2;VM1;VM2")
    if mode == 'I–V Sweep':
        # The instrument accepted these channel settings one at a time but
        # rejected the same settings in one combined DE command (0x4B).
        # Match the verified individually transmitted sequence for I–V only.
        cmds = [
            f"DE CH1,'{cfg['v1']}','{cfg['i1']}',{ch_code},{f_code}",
            "CH2",
            f"CH3,'{cfg['v3']}','{cfg['i3']}',3,3",
            "CH4",
            "VS1", "VS2", "VM1", "VM2",
        ]
    else:
        # Preserve the established current/voltage-vs-time workflows.
        cmds = [channels]
    if mode in ('Current vs Time', 'Voltage vs Time'):
        wait, interval, points = cfg['wait'], cfg['interval'], cfg['points']
        if not 0 <= wait <= 100 or not .01 <= interval <= 10 or not 1 <= points <= 512:
            raise ValueError('Time parameters outside instrument limits')
        if mode == 'Current vs Time':
            cmds.append(f"SS VC1,0,{cfg['current_limit_a']:.6g}")
        else:
            # IC1, current source setpoint (A), voltage compliance (V).
            cmds.append(f"SS IC1,0,{cfg['voltage_limit_v']:.6g}")
        cmds.append(f"SM WT{wait:.2f};IN{interval:.2f};NR{points};DM1")
        expected = points
    else:
        start, stop, step = cfg['sweep_start'], cfg['sweep_stop'], cfg['sweep_step']
        if not 0 <= start < stop <= .5:
            raise ValueError('Initial solar-cell sweep limited to 0–0.5 V')
        if not 0 < step <= .1:
            raise ValueError('Sweep step must be >0 and ≤0.1 V')
        expected = round((stop-start)/step)+1
        if expected > 201 or abs(start+(expected-1)*step-stop)>1e-8:
            raise ValueError('Sweep must reach STOP evenly in no more than 201 points')
        # Manual format: SS VR <sweep_mode=1>,start,stop,step,compliance.
        cmds.append(f"SS VR1,{start:.6g},{stop:.6g},{step:.6g},{cfg['current_limit_a']:.6g}")
        cmds.append('SM DM1')
    cmds.extend(graph_commands(cfg))
    codes = {'SHORT':'IT1', 'MEDIUM':'IT2', 'LONG':'IT3'}
    if cfg['integration'] in codes:
        cmds.append(codes[cfg['integration']])
    return cmds, record, expected


class MeasurementSession(HP4145A):
    """Adds guarded profile handling to existing working driver."""

    def configure_profile(self, cfg, *, fixture_verified=False):
        cmds, record, expected = build_commands(cfg)
        if not (self.allow_configuration and fixture_verified):
            raise HP4145AError('Configuration requires enabled session and verified fixture')
        self._profile_armed = False
        # Clear any previously latched syntax/illegal-program flags before
        # attributing a new status bit to an individual command.
        self.last_preflight_status = self.serial_poll()
        for command in cmds:
            self._send(command)
            status = self.serial_poll()
            if status & 0x0A:
                raise HP4145AError(f'Analyzer rejected command {command!r} (status 0x{status:02X})')
        self._profile_armed = True
        return record, expected

    def acquire_profile(self, *, fixture_verified=False, timeout=60):
        if not (self.allow_configuration and fixture_verified and getattr(self,'_profile_armed',False)):
            raise HP4145AError('No verified profile configured during this connection')
        self._profile_armed = False
        self._send('MD ME1')
        return self.wait_data_ready(timeout=timeout)
