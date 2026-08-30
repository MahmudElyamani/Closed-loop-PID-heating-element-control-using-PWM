import sys
import time
import serial
import serial.tools.list_ports
from collections import deque

from PyQt6.QtWidgets import (
    QApplication,
    QWidget,
    QVBoxLayout,
    QLabel,
    QGridLayout,
    QMessageBox
)

from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QFont

import pyqtgraph as pg


# ============================================================
# SETTINGS
# ============================================================

BAUD_RATE = 115200
MAX_SAMPLES = 300

# Data format expected from Arduino:
#
# temperature,setpoint,heaterState
#
# Example:
# 84.2,85.0,1
#
# heaterState:
# 1 = ON
# 0 = OFF


# ============================================================
# AUTO SERIAL DETECTION
# ============================================================

def find_arduino_port():

    ports = serial.tools.list_ports.comports()

    for port in ports:

        description = port.description.lower()

        keywords = [
            "arduino",
            "ch340",
            "cp210",
            "usb serial",
            "stm",
            "cdc"
        ]

        if any(keyword in description for keyword in keywords):
            return port.device

    return None


# ============================================================
# MAIN WINDOW
# ============================================================

class HeaterDashboard(QWidget):

    def __init__(self):

        super().__init__()

        self.serialPort = None

        self.init_ui()

        self.connect_serial()

        self.init_timer()

    # ========================================================
    # UI
    # ========================================================

    def init_ui(self):

        self.setWindowTitle("Heater Controller Dashboard")

        self.setGeometry(100, 100, 1400, 800)

        self.setStyleSheet("""

            QWidget {
                background-color: #1e1e1e;
                color: white;
            }

            QLabel {
                color: white;
            }

        """)

        mainLayout = QVBoxLayout()

        self.setLayout(mainLayout)

        # ====================================================
        # TOP PANEL
        # ====================================================

        topGrid = QGridLayout()

        # Current Temperature
        self.tempLabel = QLabel("--.- °C")
        self.tempLabel.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.tempLabel.setFont(QFont("Arial", 42, QFont.Weight.Bold))

        # Setpoint
        self.setpointLabel = QLabel("Setpoint: --.- °C")
        self.setpointLabel.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setpointLabel.setFont(QFont("Arial", 24))

        # Heater State
        self.heaterLabel = QLabel("HEATER OFF")
        self.heaterLabel.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.heaterLabel.setFont(QFont("Arial", 28, QFont.Weight.Bold))

        # Serial Status
        self.serialStatusLabel = QLabel("Disconnected")
        self.serialStatusLabel.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.serialStatusLabel.setFont(QFont("Arial", 18))

        topGrid.addWidget(self.tempLabel, 0, 0)
        topGrid.addWidget(self.setpointLabel, 0, 1)
        topGrid.addWidget(self.heaterLabel, 0, 2)
        topGrid.addWidget(self.serialStatusLabel, 1, 0, 1, 3)

        mainLayout.addLayout(topGrid)

        # ====================================================
        # GRAPH
        # ====================================================

        self.graphWidget = pg.PlotWidget()

        mainLayout.addWidget(self.graphWidget)

        self.graphWidget.setBackground("#1e1e1e")

        self.graphWidget.showGrid(x=True, y=True)

        self.graphWidget.setTitle(
            "Temperature vs Time",
            color="white",
            size="20pt"
        )

        styles = {
            "color": "white",
            "font-size": "14px"
        }

        self.graphWidget.setLabel(
            "left",
            "Temperature (°C)",
            **styles
        )

        self.graphWidget.setLabel(
            "bottom",
            "Samples",
            **styles
        )

        self.graphWidget.addLegend()

        # ====================================================
        # DATA BUFFERS
        # ====================================================

        self.xData = deque(maxlen=MAX_SAMPLES)

        self.tempData = deque(maxlen=MAX_SAMPLES)

        self.setpointData = deque(maxlen=MAX_SAMPLES)

        self.sampleNumber = 0

        # ====================================================
        # PLOT LINES
        # ====================================================

        self.tempLine = self.graphWidget.plot(
            pen=pg.mkPen(width=3),
            name="Temperature"
        )

        self.setpointLine = self.graphWidget.plot(
            pen=pg.mkPen(
                style=Qt.PenStyle.DashLine,
                width=2
            ),
            name="Setpoint"
        )

        # ====================================================
        # CSV LOGGING
        # ====================================================

        timestamp = time.strftime("%Y%m%d_%H%M%S")

        self.logFile = open(
            f"temperature_log_{timestamp}.csv",
            "w"
        )

        self.logFile.write(
            "Sample,Temperature,Setpoint,Heater\n"
        )

    # ========================================================
    # SERIAL CONNECTION
    # ========================================================

    def connect_serial(self):

        try:

            port = find_arduino_port()

            if port is None:

                self.serialStatusLabel.setText(
                    "Arduino Not Detected"
                )

                self.serialStatusLabel.setStyleSheet(
                    "color: red;"
                )

                return

            self.serialPort = serial.Serial(
                port,
                BAUD_RATE,
                timeout=1
            )

            time.sleep(2)

            self.serialStatusLabel.setText(
                f"Connected: {port}"
            )

            self.serialStatusLabel.setStyleSheet(
                "color: lime;"
            )

            print(f"Connected to {port}")

        except Exception as e:

            print(e)

            self.serialStatusLabel.setText(
                "Connection Failed"
            )

            self.serialStatusLabel.setStyleSheet(
                "color: red;"
            )

    # ========================================================
    # TIMER
    # ========================================================

    def init_timer(self):

        self.timer = QTimer()

        self.timer.timeout.connect(
            self.update_data
        )

        self.timer.start(50)

    # ========================================================
    # UPDATE LOOP
    # ========================================================

    def update_data(self):

        try:

            # Reconnect if disconnected
            if self.serialPort is None:

                self.connect_serial()

                return

            if not self.serialPort.is_open:

                self.connect_serial()

                return

            if self.serialPort.in_waiting:

                line = self.serialPort.readline() \
                    .decode(errors='ignore') \
                    .strip()

                parts = line.split(",")

                if len(parts) != 3:
                    return

                temperature = float(parts[0])

                setpoint = float(parts[1])

                heaterState = int(parts[2])

                # ============================================
                # UPDATE LABELS
                # ============================================

                self.tempLabel.setText(
                    f"{temperature:.1f} °C"
                )

                self.setpointLabel.setText(
                    f"Setpoint: {setpoint:.1f} °C"
                )

                if heaterState:

                    self.heaterLabel.setText(
                        "HEATER ON"
                    )

                    self.heaterLabel.setStyleSheet(
                        "color: lime;"
                    )

                else:

                    self.heaterLabel.setText(
                        "HEATER OFF"
                    )

                    self.heaterLabel.setStyleSheet(
                        "color: red;"
                    )

                # ============================================
                # STORE DATA
                # ============================================

                self.xData.append(self.sampleNumber)

                self.tempData.append(temperature)

                self.setpointData.append(setpoint)

                self.sampleNumber += 1

                # ============================================
                # UPDATE GRAPH
                # ============================================

                self.tempLine.setData(
                    list(self.xData),
                    list(self.tempData)
                )

                self.setpointLine.setData(
                    list(self.xData),
                    list(self.setpointData)
                )

                # ============================================
                # CSV LOGGING
                # ============================================

                self.logFile.write(
                    f"{self.sampleNumber},"
                    f"{temperature},"
                    f"{setpoint},"
                    f"{heaterState}\n"
                )

        except Exception as e:

            print("Serial Error:", e)

            try:

                if self.serialPort:
                    self.serialPort.close()

            except:
                pass

            self.serialPort = None

            self.serialStatusLabel.setText(
                "Disconnected - Reconnecting..."
            )

            self.serialStatusLabel.setStyleSheet(
                "color: orange;"
            )

    # ========================================================
    # CLOSE EVENT
    # ========================================================

    def closeEvent(self, event):

        try:

            if self.serialPort:
                self.serialPort.close()

            self.logFile.close()

        except:
            pass

        event.accept()


# ============================================================
# MAIN
# ============================================================

if __name__ == "__main__":

    app = QApplication(sys.argv)

    pg.setConfigOptions(antialias=True)

    window = HeaterDashboard()

    window.show()

    sys.exit(app.exec())