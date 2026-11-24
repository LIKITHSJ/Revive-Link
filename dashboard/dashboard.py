"""
Revive-Link Dashboard (simulation mode)

Displays SOS alerts from Revive-Link nodes in a table and on a map.
This version generates simulated alerts for demonstration. In the
deployed system, alerts are received by the LoRa base station and
printed over serial (see firmware/base_station).
"""

import os
import random
import threading
import time
import tkinter as tk
import webbrowser
from tkinter import ttk

import folium

MAP_FILE = "revive_link_map.html"


def generate_dummy_alerts(callback):
    """Simulate SOS alerts from three handheld nodes every 10 seconds."""
    nodes = ["Node_01", "Node_02", "Node_03"]
    while True:
        time.sleep(10)
        alert = {
            "node_id": random.choice(nodes),
            "lat": round(12.90 + random.random() * 0.1, 6),
            "lon": round(77.50 + random.random() * 0.1, 6),
            "status": "SOS",
        }
        callback(alert)


class ReviveLinkDashboard:
    def __init__(self, root):
        self.root = root
        self.root.title("Revive-Link Dashboard (Simulation Mode)")

        columns = ("Node ID", "Latitude", "Longitude", "Status")
        self.tree = ttk.Treeview(root, columns=columns, show="headings")
        for col in columns:
            self.tree.heading(col, text=col)
        self.tree.pack(fill="both", expand=True)

        tk.Button(root, text="Open Map", command=self.open_map).pack(pady=10)

        self.alerts = []

    def add_alert_threadsafe(self, alert):
        """Called from the background thread; hands the alert to the UI thread."""
        self.root.after(0, self.add_alert, alert)

    def add_alert(self, alert):
        print("New alert received:", alert)
        self.alerts.append(alert)
        self.tree.insert(
            "", "end",
            values=(alert["node_id"], alert["lat"], alert["lon"], alert["status"]),
        )
        self.update_map()

    def update_map(self):
        if not self.alerts:
            return
        latest = self.alerts[-1]
        map_obj = folium.Map(location=[latest["lat"], latest["lon"]], zoom_start=13)
        for a in self.alerts:
            folium.Marker(
                [a["lat"], a["lon"]],
                popup=f"{a['node_id']} - {a['status']}",
                icon=folium.Icon(color="red"),
            ).add_to(map_obj)
        map_obj.save(MAP_FILE)

    def open_map(self):
        if os.path.exists(MAP_FILE):
            webbrowser.open("file://" + os.path.abspath(MAP_FILE))
        else:
            print("No alerts yet - map not created.")


if __name__ == "__main__":
    root = tk.Tk()
    dashboard = ReviveLinkDashboard(root)
    threading.Thread(
        target=generate_dummy_alerts,
        args=(dashboard.add_alert_threadsafe,),
        daemon=True,
    ).start()
    root.mainloop()
