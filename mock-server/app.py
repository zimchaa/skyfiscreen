"""SkyFi ground-station mock REST server (stand-in for the Pi's real API).

Implements the v1 contract the Presto panel polls:

    GET  /api/v1/status       -> {system, drone, link, ts}
    GET  /api/v1/environment  -> {readings: [{id,label,value,unit,status,min,max}]}
    GET  /api/v1/wifi         -> {ssid, auth, password, qr}
    POST /api/v1/land         -> {accepted, command_id}

Demo controls (curl/browser):

    POST /demo/fault          -> toggle a fault scenario (gusting wind + degraded system)
    POST /demo/reset          -> back to nominal

Run:  uvicorn app:app --host 0.0.0.0 --port 8000
"""
import random
import time
import uuid

from fastapi import FastAPI
from pydantic import BaseModel

app = FastAPI(title="SkyFi mock ground station")

state = {
    "fault": False,
    "drone": "airborne",
    "landing_started": 0.0,
    "wind": 4.2,
    "temp": 21.5,
    "tether": 12.4,
    "battery": 87.0,
}


def _walk():
    gust_bias = 4.0 if state["fault"] else 0.0
    state["wind"] = max(0.5, min(14.0, state["wind"] * 0.9 + (4.0 + gust_bias) * 0.1
                                 + random.uniform(-0.8, 0.8)))
    state["temp"] += random.uniform(-0.1, 0.1)
    state["tether"] = max(10.0, state["tether"] + random.uniform(-0.3, 0.3))
    state["battery"] = max(0.0, state["battery"] - 0.005)
    # auto-complete a landing after 20s
    if state["drone"] == "landing" and time.time() - state["landing_started"] > 20:
        state["drone"] = "grounded"


def _sev(value, warn, danger):
    return "fault" if value >= danger else "warn" if value >= warn else "ok"


@app.get("/api/v1/status")
def status():
    _walk()
    wind_bad = state["wind"] > 9.5
    system = "fault" if wind_bad else ("degraded" if state["fault"] or state["wind"] > 7 else "ok")
    return {"system": system, "drone": state["drone"], "link": "ok", "ts": int(time.time())}


@app.get("/api/v1/environment")
def environment():
    _walk()
    return {"readings": [
        {"id": "wind", "label": "WIND", "value": round(state["wind"], 1), "unit": "m/s",
         "status": _sev(state["wind"], 7.0, 9.5), "min": 0, "max": 15},
        {"id": "temp", "label": "TEMP", "value": round(state["temp"], 1), "unit": "C",
         "status": "ok", "min": -10, "max": 45},
        {"id": "tether", "label": "TETHER", "value": round(state["tether"], 1), "unit": "kg",
         "status": _sev(state["tether"], 16.0, 20.0), "min": 0, "max": 25},
        {"id": "battery", "label": "BATTERY", "value": round(state["battery"]), "unit": "%",
         "status": "fault" if state["battery"] < 20 else "warn" if state["battery"] < 40 else "ok",
         "min": 0, "max": 100},
    ]}


@app.get("/api/v1/wifi")
def wifi():
    ssid, password = "SkyFi-Ground", "skyfi-field-1234"
    return {"ssid": ssid, "auth": "WPA2", "password": password,
            "qr": f"WIFI:T:WPA;S:{ssid};P:{password};;"}


class LandRequest(BaseModel):
    source: str = "unknown"
    reason: str = ""
    confirm: bool = False


@app.post("/api/v1/land")
def land(req: LandRequest):
    if not req.confirm:
        return {"accepted": False, "error": "confirm required"}
    state["drone"] = "landing"
    state["landing_started"] = time.time()
    cid = str(uuid.uuid4())[:8]
    print(f"*** LAND COMMAND accepted from {req.source!r} ({req.reason!r}) -> {cid}")
    return {"accepted": True, "command_id": cid}


@app.post("/demo/fault")
def demo_fault():
    state["fault"] = not state["fault"]
    return {"fault": state["fault"]}


@app.post("/demo/reset")
def demo_reset():
    state.update(fault=False, drone="airborne", wind=4.2, battery=87.0)
    return {"ok": True}
