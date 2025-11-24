import pandas as pd
import matplotlib.pyplot as plt

# ===============================
# CONFIGURACIÓN
# ===============================

archivos = {
    "Pérdida 0%": "tp_d_redes/tests_p2/delays/delays_0%.csv",
    "Pérdida 1%": "tp_d_redes/tests_p2/delays/delays_1%.csv",
    "Pérdida 2%": "tp_d_redes/tests_p2/delays/delays_2%.csv",
    "Pérdida 5%": "tp_d_redes/tests_p2/delays/delays_5%.csv",
    "Jitter (50ms)": "tp_d_redes/tests_p2/delays/delays_con_jitter.csv"
}

# Colores similares a tu imagen
colores = {
    "Pérdida 0%": "lime",
    "Pérdida 1%": "cyan",
    "Pérdida 2%": "yellow",
    "Pérdida 5%": "orange",
    "Jitter (50ms)": "magenta"
}

plt.style.use("dark_background")

# Crear figura y ejes
fig, axs = plt.subplots(len(archivos), 1, figsize=(12, 8), sharex=True)

# Forzar lista siempre
if len(archivos) == 1:
    axs = [axs]

# ===============================
# GRAFICAR
# ===============================

for i, (titulo, archivo) in enumerate(archivos.items()):
    df = pd.read_csv(archivo, header=None, names=["idx", "delay"])

  

    axs[i].plot(df["idx"], df["delay"], color=colores[titulo], lw=2)
    axs[i].set_title(titulo, color=colores[titulo], fontsize=14)
    axs[i].set_ylabel("Delay [s]")
    axs[i].set_ylim(-.1, 1)
    axs[i].grid(True, linestyle="--", alpha=0.3)

    if titulo == "Jitter (50ms)":
        axs[i].set_ylim(-.1, 1)
        

axs[-1].set_xlabel("Número de Medición (Paquete)")

plt.tight_layout()
plt.savefig("delays_plot.png", dpi=300)
plt.show()
