# y_axis.py

class YAxis:
    def __init__(self, steps_per_rev=200, microsteps=1, lead_mm=1.0):

        self.steps_per_rev = steps_per_rev
        self.microsteps = microsteps
        self.lead_mm = lead_mm

        self.steps_per_mm = (
            self.steps_per_rev * self.microsteps / self.lead_mm
        )

        self.position_mm = 0.0
        self.motor_steps = 0

    # --------------------------------------------------
    # Conversion functions
    # --------------------------------------------------

    def mm_to_steps(self, distance_mm):
        return round(distance_mm * self.steps_per_mm)

    def steps_to_mm(self, steps):
        return steps / self.steps_per_mm

    # --------------------------------------------------
    # Y-axis operations
    # --------------------------------------------------

    def home(self):

        print("Y-axis homing...")

        # Hardware homing will be added later
        self.position_mm = 0.0
        self.motor_steps = 0

        print("Y-axis home position: 0 mm")

    def move(self, distance_mm):

        steps = self.mm_to_steps(distance_mm)

        print(
            f"Y moving {distance_mm} mm "
            f"({steps} steps)"
        )

        # Hardware motor command will go here

        self.position_mm += distance_mm
        self.motor_steps += steps

        print(
            f"Y position = {self.position_mm:.3f} mm"
        )

    def move_to(self, target_mm):

        distance = target_mm - self.position_mm

        self.move(distance)

    def return_home(self):

        self.move_to(0)

    def get_position(self):

        return self.position_mm

    def get_motor_steps(self):

        return self.motor_steps
