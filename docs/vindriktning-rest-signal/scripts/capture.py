"""Capture one Vindriktning frame on both scope channels and save it as JSON.

Sets both channels to 1 V/div with 0 V two divisions above the bottom, 2 ms/div with the trigger near the
left edge, and a single-shot trigger on a falling edge of CH1 at half its idle level. Prints the scope's own
level measurements of the capture, then saves the screen waveform of both channels (1200 points each).

With --hold-esp-in-reset, the ESP32-C3 on /dev/ttyACM0 is held in reset during the capture through its
USB-Serial-JTAG port (RTS asserted, DTR not), and let go afterwards. That needs pyserial.

    python3 capture.py [--hold-esp-in-reset] capture.json
"""
import json
import sys
import time

from rigol import Scope

hold_reset = "--hold-esp-in-reset" in sys.argv
out = [a for a in sys.argv[1:] if not a.startswith("--")][0]

port = None
if hold_reset:
    import serial

    port = serial.Serial()
    port.port = "/dev/ttyACM0"
    port.dtr = False
    port.rts = False
    port.open()
    port.rts = True
    time.sleep(0.5)
    print("ESP held in reset")

scope = Scope()
try:
    scope.write(":TRIG:SWE AUTO")
    scope.write(":RUN")
    for ch in ("CHAN1", "CHAN2"):
        scope.write(f":{ch}:DISP ON")
        scope.write(f":{ch}:COUP DC")
        scope.write(f":{ch}:SCAL 1")
        scope.write(f":{ch}:OFFS -2.5")
    scope.write(":TIM:MAIN:SCAL 0.002")
    scope.write(":TIM:MAIN:OFFS 0.010")
    time.sleep(2)
    for ch in ("CHAN1", "CHAN2"):
        print("idle", ch, "probe", scope.query(f":{ch}:PROB?"), scope.measure(ch, ("VMAX", "VMIN", "VAVG")))

    level = round(float(scope.query(":MEAS:VMAX? CHAN1")) / 2, 2)
    print("trigger level", level)
    scope.write(":TRIG:MODE EDGE")
    scope.write(":TRIG:EDG:SOUR CHAN1")
    scope.write(":TRIG:EDG:SLOP NEG")
    scope.write(f":TRIG:EDG:LEV {level}")
    scope.write(":TRIG:SWE SING")
    time.sleep(0.5)
    scope.write(":SING")
    time.sleep(1)
    start = time.time()
    while scope.query(":TRIG:STAT?") != "STOP":
        if time.time() - start > 90:
            sys.exit("no trigger within 90 s")
        time.sleep(0.5)
    print(f"triggered after {time.time() - start:.1f} s")

    result = {}
    for ch in ("CHAN1", "CHAN2"):
        print("frame", ch, scope.measure(ch))
        result[ch] = scope.read_waveform(ch)
    json.dump(result, open(out, "w"), separators=(",", ":"))
finally:
    scope.close()
    if port is not None:
        port.rts = False
        port.close()
