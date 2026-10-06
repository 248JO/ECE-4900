# x_axis.py

class XAxis:
    def __init__(self, steps_per_rev=200, microsteps=1, lead_mm=1.0):
        """
        X-axis configuration.

        steps_per_rev: Full motor steps per revolution
        microsteps: Driver microstepping setting
        lead_mm: Linear travel per motor revolution (mm)
        """

        self.steps_per_rev = steps_per_rev
        self.microsteps = microsteps
        self.lead_mm = lead_mm

        # Calculate steps required to move 1 mm
        self.steps_per_mm = (
            self.steps_per_rev * self.microsteps / self.lead_mm
        )

        # Current X position
        self.position_mm = 0.0

        # Total motor steps from origin
        self.motor_steps = 0

    # --------------------------------------------------
    # Conversion functions
    # --------------------------------------------------

    def mm_to_steps(self, distance_mm):
        """Convert linear distance (mm) to motor steps."""
        return round(distance_mm * self.steps_per_mm)

    def steps_to_mm(self, steps):
        """Convert motor steps to linear distance (mm)."""
        return steps / self.steps_per_mm

    # --------------------------------------------------
    # X-axis operations
    # --------------------------------------------------

    def home(self):
        """
        Home X axis.

        Later:
        This function will move the motor toward
        the X limit switch.
        """

        print("X-axis homing...")

        # Hardware homing will be added later
        self.position_mm = 0.0
        self.motor_steps = 0

        print("X-axis home position: 0 mm")

    def move(self, distance_mm):
        """Move X axis by a specified distance."""

        steps = self.mm_to_steps(distance_mm)

        print(
            f"X moving {distance_mm} mm "
            f"({steps} steps)"
        )

        # Hardware motor command will go here

        self.position_mm += distance_mm
        self.motor_steps += steps

        print(
            f"X position = {self.position_mm:.3f} mm"
        )

    def move_to(self, target_mm):
        """Move X axis to an absolute position."""

        distance = target_mm - self.position_mm

        self.move(distance)

    def return_home(self):
        """Return X axis to origin."""

        self.move_to(0)

    def get_position(self):
        """Return current X position."""

        return self.position_mm

    def get_motor_steps(self):
        """Return current motor step count."""

        return self.motor_steps
