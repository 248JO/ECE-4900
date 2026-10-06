# main.py

from x_axis import XAxis
from y_axis import YAxis
from z_axis import ZAxis


# ==================================================
# SYSTEM PARAMETERS
# ==================================================

# Motor parameters
STEPS_PER_REV = 200
MICROSTEPS = 1

# Lead screw
LEAD_MM = 1.0


# ==================================================
# EXPERIMENT PARAMETERS
# ==================================================

# X experiment
X_START = 20.0       # mm
X_INCREMENT = 1.0    # mm
X_POINTS = 5


# Y experiment
Y_START = 20.0       # mm
Y_INCREMENT = 1.0    # mm
Y_POINTS = 5


# Z camera-distance experiment
Z_DISTANCES = [
    5.0,
    6.0,
    7.0,
    8.0,
    9.0
]


# ==================================================
# CREATE AXIS OBJECTS
# ==================================================

x_axis = XAxis(
    steps_per_rev=STEPS_PER_REV,
    microsteps=MICROSTEPS,
    lead_mm=LEAD_MM
)

y_axis = YAxis(
    steps_per_rev=STEPS_PER_REV,
    microsteps=MICROSTEPS,
    lead_mm=LEAD_MM
)

z_axis = ZAxis(
    steps_per_rev=STEPS_PER_REV,
    microsteps=MICROSTEPS,
    lead_mm=LEAD_MM
)


# ==================================================
# HOME ALL AXES
# ==================================================

def home_all():

    print("\n======================")
    print("HOMING ALL AXES")
    print("======================")

    x_axis.home()
    y_axis.home()
    z_axis.home()


# ==================================================
# X AXIS EXPERIMENT
# ==================================================

def run_x_experiment():

    print("\n======================")
    print("X AXIS EXPERIMENT")
    print("======================")

    x_axis.move_to(X_START)

    for i in range(X_POINTS):

        print(
            f"\nX measurement {i + 1}"
        )

        print(
            f"X coordinate = "
            f"{x_axis.get_position()} mm"
        )

        print(
            f"X motor steps = "
            f"{x_axis.get_motor_steps()}"
        )

        # Camera operation will go here later

        x_axis.move(X_INCREMENT)

    x_axis.return_home()


# ==================================================
# Y AXIS EXPERIMENT
# ==================================================

def run_y_experiment():

    print("\n======================")
    print("Y AXIS EXPERIMENT")
    print("======================")

    y_axis.move_to(Y_START)

    for i in range(Y_POINTS):

        print(
            f"\nY measurement {i + 1}"
        )

        print(
            f"Y coordinate = "
            f"{y_axis.get_position()} mm"
        )

        print(
            f"Y motor steps = "
            f"{y_axis.get_motor_steps()}"
        )

        # Camera operation will go here later

        y_axis.move(Y_INCREMENT)

    y_axis.return_home()


# ==================================================
# Z AXIS EXPERIMENT
# ==================================================

def run_z_experiment():

    print("\n======================")
    print("Z AXIS CAMERA EXPERIMENT")
    print("======================")

    z_axis.camera_distance_test(
        Z_DISTANCES
    )


# ==================================================
# DISPLAY FINAL POSITION
# ==================================================

def show_position():

    print("\n======================")
    print("CURRENT MACHINE POSITION")
    print("======================")

    print(
        f"X = {x_axis.get_position():.3f} mm"
    )

    print(
        f"Y = {y_axis.get_position():.3f} mm"
    )

    print(
        f"Z = {z_axis.get_position():.3f} mm"
    )


# ==================================================
# MAIN PROGRAM
# ==================================================

def main():

    print("==============================")
    print("XYZ GANTRY CONTROL SYSTEM")
    print("==============================")

    # 1. Home entire machine
    home_all()

    # 2. Run X experiment
    run_x_experiment()

    # 3. Run Y experiment
    run_y_experiment()

    # 4. Run Z camera experiment
    run_z_experiment()

    # 5. Return everything home
    home_all()

    # 6. Display final coordinates
    show_position()

    print("\nExperiment complete.")


# ==================================================
# START PROGRAM
# ==================================================

if __name__ == "__main__":
    main()
