"""Affiche les 120 derniers releves de masse transmis par l'Arduino."""
import argparse
from collections import deque
import time

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from matplotlib.widgets import Button
import serial

analyseur_arguments = argparse.ArgumentParser(description=__doc__)
analyseur_arguments.add_argument("port", help="Port serie de l'Arduino, par exemple COM3")
arguments = analyseur_arguments.parse_args()
port_serie = serial.Serial(arguments.port, 9600, timeout=0)
# L'ouverture du port peut redemarrer un Uno ; attendre la tare initiale.
time.sleep(3)
port_serie.write(b"H")
points = deque(maxlen=120)
tampon = bytearray()
figure, axes = plt.subplots()
figure.subplots_adjust(bottom=0.25)
lecture = figure.text(0.12, 0.04, "")
axes_bouton = figure.add_axes([0.60, 0.03, 0.30, 0.07])
bouton = Button(axes_bouton, "Lire la masse")
bouton.on_clicked(lambda evenement: port_serie.write(b"M"))
courbe, = axes.plot([], [], ".-", linewidth=1)
axes.set_xlabel("Temps depuis le premier point conserve (min)")
axes.set_ylabel("Masse (g)")
axes.grid(True, alpha=0.3)
axes.set_title("Derive de la balance - attente du premier releve (1 min)")


def update(_):
    tampon.extend(port_serie.read(port_serie.in_waiting))
    while b"\n" in tampon:
        donnees_brutes, _, reste = tampon.partition(b"\n")
        tampon[:] = reste
        texte = donnees_brutes.decode("ascii", errors="replace").strip()
        if texte.startswith("Masse instantanee:") or texte.startswith("Erreur"):
            lecture.set_text(texte)
            print(texte)
            continue
        if texte == "BEGIN":
            points.clear()
            continue
        try:
            horodatage, masse = texte.split(",")
            horodatage, masse = int(horodatage), float(masse)
        except ValueError:
            if texte and texte != "END":
                print(texte)
            continue
        points.append((horodatage, masse))
    if points:
        # Soustraction modulo 32 bits pour supporter le debordement de millis().
        minutes = [((instant - points[0][0]) % (2**32)) / 60000 for instant, _ in points]
        masses = [masse_point for _, masse_point in points]
        courbe.set_data(minutes, masses)
        axes.relim()
        axes.autoscale_view()
        variation = masses[-1] - masses[0]
        axes.set_title(f"Derive de la balance - {len(points)}/120 points | variation : {variation:+.3f} g")
    return (courbe,)


animation = FuncAnimation(figure, update, interval=1000, cache_frame_data=False)
try:
    plt.show()
finally:
    port_serie.close()
