import sys
import os
import subprocess
import time

from PyQt5.QtWidgets import QApplication, QMainWindow, QFileDialog, QMessageBox
from PyQt5.QtCore import QThread, pyqtSignal

from interfaz import Ui_MainWindow


class WorkerThread(QThread):
    output_signal = pyqtSignal(str)
    progress_signal = pyqtSignal(int)
    finished_signal = pyqtSignal()
    tiempo_restante_signal = pyqtSignal(float)

    def __init__(self, carpeta, kernel, exe_name, total_salidas, nprocs=19, parent=None):
        super().__init__(parent)
        self.carpeta = carpeta
        self.kernel = kernel
        self.exe_name = exe_name
        self.nprocs = nprocs
        self.total_salidas = total_salidas
        self._stopped = False
        self.start_time = None
        self.salidas_detectadas = 0

    def run(self):
        cmd = [
            "mpiexec",
            "-n", str(self.nprocs),
            "-f", "machinefile",
            f"./{self.exe_name}",
            str(self.kernel),
        ]
        try:
            proceso = subprocess.Popen(
                cmd,
                cwd=self.carpeta,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
                universal_newlines=True,
            )
        except FileNotFoundError:
            self.output_signal.emit(f"ERROR: No se pudo ejecutar '{cmd[4]}' o no se encontr\u00f3 'machinefile'\n")
            self.finished_signal.emit()
            return

        self.start_time = time.time()

        for linea in iter(proceso.stdout.readline, ''):
            if self._stopped:
                break

            texto = linea.rstrip()
            self.output_signal.emit(texto)

            if texto.startswith("[Rank"):
                if "blur aplicado" in texto.lower():
                    self.salidas_detectadas += 1
                elif "filtros ligeros procesados" in texto.lower():
                    self.salidas_detectadas += 5

            porcentaje = int((self.salidas_detectadas / self.total_salidas) * 100)
            if porcentaje < 100:
                self.progress_signal.emit(porcentaje)

            elapsed = time.time() - self.start_time
            if self.salidas_detectadas > 0:
                estimado_total = (elapsed / self.salidas_detectadas) * self.total_salidas
                restante = max(0.0, estimado_total - elapsed)
                self.tiempo_restante_signal.emit(restante)

        return_code = proceso.wait()
        proceso.stdout.close()

        self.progress_signal.emit(100)
        self.tiempo_restante_signal.emit(0.0)

        if return_code == 0:
            try:
                results_path = os.path.join(self.carpeta, "results.txt")
                for _ in range(30):
                    if os.path.exists(results_path):
                        break
                    time.sleep(0.1)

                if os.path.exists(results_path):
                    with open(results_path, "r") as f:
                        contenido = f.read()
                        self.output_signal.emit("\n--- Resultados del procesamiento ---\n")
                        self.output_signal.emit(contenido)

                    aws_costos = """
--- Costos AWS ---

Instancia: c6g.4xlarge
Tenancy: Shared Instances
Sistema Operativo: Linux
EBS Storage: 30 GB
Transferencia de datos: 12 GB/mes
Monitoreo: Desactivado
Capacidad de procesamiento: Lunes a Viernes, 8 horas diarias

| Concepto           | Costo (USD) |
| ------------------ | ----------- |
| Upfront cost anual | $7,541.28   |
| Costo mensual      | $628.44     |

Detalles adicionales:
- Workload: Diario (lunes a viernes)
- Horario pico: 8 horas por d\u00eda
- Pricing: Amazon EC2 Savings Plans 3yr All Upfront
- DT Inbound: 0 TB/mes
- DT Outbound Internet: 12 GB/mes
- DT Intra-Region: 0 TB/mes
"""
                    self.output_signal.emit(aws_costos)
                else:
                    self.output_signal.emit("Advertencia: results.txt no fue encontrado a tiempo.")
            except Exception as e:
                self.output_signal.emit(f"ERROR al leer results.txt: {e}")
        else:
            self.output_signal.emit("ERROR: El proceso termin\u00f3 con un c\u00f3digo distinto de 0.")

        self.finished_signal.emit()

    def stop(self):
        self._stopped = True
        self.terminate()


class MainApp(QMainWindow):
    def __init__(self):
        super().__init__()
        self.ui = Ui_MainWindow()
        self.ui.setupUi(self)

        self.ui.selectFolder.clicked.connect(self.seleccionar_folder)
        self.ui.ejecutarButton.clicked.connect(self.iniciar_proceso)
        self.ui.showResultFolder.clicked.connect(self.abrir_carpeta_resultados)
        self.ui.showResultFolder.setEnabled(False)
        self.ui.progressBar.setValue(0)
        self.ui.folderDLine.setReadOnly(True)

        self.worker = None
        self.total_salidas = 1

    def seleccionar_folder(self):
        carpeta = QFileDialog.getExistingDirectory(
            self, "Selecciona la carpeta del proyecto", os.getcwd()
        )
        if carpeta:
            self.ui.folderILine.setText(carpeta)

    def iniciar_proceso(self):
        carpeta_imagenes = self.ui.folderILine.text().strip()

        if not carpeta_imagenes or not os.path.isdir(carpeta_imagenes):
            QMessageBox.warning(self, "Carpeta inv\u00e1lida", "Selecciona primero una carpeta v\u00e1lida.")
            return

        kernel_str = self.ui.kernelLine.text().strip()
        try:
            kernel = int(kernel_str)
            if kernel < 55 or kernel > 150 or (kernel % 2 == 0):
                raise ValueError
        except ValueError:
            QMessageBox.warning(
                self,
                "Kernel inv\u00e1lido",
                "El tama\u00f1o de kernel debe ser un n\u00famero impar entre 55 y 150.",
            )
            return

        exe_name = "procesador"
        carpeta_padre = os.path.dirname(carpeta_imagenes)
        exe_path = os.path.join(carpeta_padre, exe_name)

        if not os.path.isfile(exe_path) or not os.access(exe_path, os.X_OK):
            QMessageBox.critical(
                self,
                "Ejecutable no encontrado",
                f"No se encontr\u00f3 el ejecutable '{exe_name}' en:\n  {carpeta_padre}\n"
                "Aseg\u00farate de que el binario est\u00e9 en la carpeta padre de la carpeta de im\u00e1genes.",
            )
            return

        try:
            archivos = os.listdir(carpeta_imagenes)
            bmp_files = [f for f in archivos if f.lower().endswith(".bmp")]
            total_imagenes = len(bmp_files)
        except Exception as e:
            QMessageBox.critical(self, "Error", f"No se pudo leer la carpeta:\n{carpeta_imagenes}\n{e}")
            return

        if total_imagenes == 0:
            QMessageBox.warning(self, "Sin im\u00e1genes", "La carpeta 'images/' no contiene im\u00e1genes BMP.")
            return

        self.total_salidas = total_imagenes * 6

        self.ui.selectFolder.setEnabled(False)
        self.ui.ejecutarButton.setEnabled(False)
        self.ui.showResultFolder.setEnabled(False)
        self.ui.infoText.clear()
        self.ui.progressBar.setMaximum(100)
        self.ui.progressBar.setValue(0)
        self.ui.label.setText("Tiempo restante: -- s")

        self.worker = WorkerThread(
            carpeta_padre,
            kernel,
            exe_name,
            total_salidas=self.total_salidas,
            nprocs=19,
            parent=self
        )

        self.worker.output_signal.connect(self.append_output)
        self.worker.progress_signal.connect(self.update_progress)
        self.worker.finished_signal.connect(self.proceso_terminado)
        self.worker.tiempo_restante_signal.connect(self.actualizar_tiempo_restante)

        self.worker.start()

    def append_output(self, linea: str):
        self.ui.infoText.append(linea)
        cursor = self.ui.infoText.textCursor()
        cursor.movePosition(cursor.End)
        self.ui.infoText.setTextCursor(cursor)

    def update_progress(self, valor: int):
        self.ui.progressBar.setMaximum(100)
        self.ui.progressBar.setValue(valor)

    def actualizar_tiempo_restante(self, segundos: float):
        self.ui.label.setText(f"Tiempo restante: {segundos:.1f} s")

    def proceso_terminado(self):
        self.ui.progressBar.setMaximum(100)
        self.ui.progressBar.setValue(100)
        self.ui.label.setText("Tiempo restante: 0.0 s")
        self.ui.showResultFolder.setEnabled(True)
        self.ui.selectFolder.setEnabled(True)
        self.ui.ejecutarButton.setEnabled(True)

        QMessageBox.information(self, "Terminado", "El procesamiento ha finalizado.")

    def abrir_carpeta_resultados(self):
        carpeta_imagenes = self.ui.folderILine.text().strip()
        carpeta_padre = os.path.dirname(carpeta_imagenes)
        results_folder = os.path.join(carpeta_padre, "results")

        for _ in range(100):
            if os.path.isdir(results_folder):
                break
            time.sleep(0.1)
        else:
            QMessageBox.warning(self, "Error", "La carpeta 'results/' no fue encontrada a tiempo.")
            return

        self.ui.folderDLine.setText(results_folder)

        display_ok = os.environ.get("DISPLAY") or sys.platform.startswith(("win", "darwin"))
        if not display_ok:
            QMessageBox.information(
                self,
                "Carpeta lista",
                f"La carpeta de resultados est\u00e1 disponible en:\n{results_folder}\n"
                "Pero no se puede abrir autom\u00e1ticamente sin entorno gr\u00e1fico."
            )
            return

        try:
            if sys.platform.startswith("darwin"):
                subprocess.call(["open", results_folder])
            elif sys.platform.startswith("win"):
                os.startfile(results_folder)
            else:
                subprocess.Popen(["xdg-open", results_folder])
        except Exception as e:
            QMessageBox.warning(self, "Error", f"No se pudo abrir la carpeta:\n{e}")


if __name__ == "__main__":
    app = QApplication(sys.argv)
    ventana = MainApp()
    ventana.setWindowTitle("Procesador de im\u00e1genes en paralelo")
    ventana.show()
    sys.exit(app.exec())
