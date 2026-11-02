#!/usr/bin/env python3
"""Messprototyp Variante C: Python mit PyQt6 (Debian-Pakete), QWidget + QPainter, Qt Multimedia.

Gleicher Aufbau wie Variante A (prototypes/a-cpp-qt/main.cpp), nur für T-01 und T-02.

  --mode t01                       Fenster zeigen, nach dem ersten Bild 1 s warten, beenden.
                                   Gibt "FIRST_FRAME_NS <CLOCK_MONOTONIC ns>" aus.
  --mode t02 --media DIR --stats DATEI --duration S
                                   Overlay 1920x1080 im Vollbild, Ziel 30 Bilder/s.

Quellen:
  PyQt6:              https://www.riverbankcomputing.com/static/Docs/PyQt6/
  QMediaPlayer/Sink:  https://doc.qt.io/qt-6.8/qmediaplayer.html, https://doc.qt.io/qt-6.8/qvideosink.html
  time.monotonic_ns:  https://docs.python.org/3/library/time.html#time.monotonic_ns (CLOCK_MONOTONIC unter Linux)
"""
import argparse
import json
import math
import sys
import time

from PyQt6.QtCore import QPoint, QPointF, QRect, Qt, QTimer, QUrl
from PyQt6.QtGui import QColor, QFont, QImage, QPainter, QPainterPath, QPen, QPixmap
from PyQt6.QtMultimedia import QMediaPlayer, QVideoFrameFormat, QVideoSink
from PyQt6.QtWidgets import QApplication, QLabel, QWidget


def epoch_ms():
    return time.time() * 1000.0


class StartWindow(QLabel):
    def __init__(self):
        super().__init__("T-01 Variante C (Python/PyQt6)")
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.resize(800, 450)
        self.setWindowTitle("P01 T-01 C")
        self._done = False

    def paintEvent(self, e):
        super().paintEvent(e)
        if not self._done:
            self._done = True
            QTimer.singleShot(0, self._first_frame)

    def _first_frame(self):
        print(f"FIRST_FRAME_NS {time.monotonic_ns()}", flush=True)
        QTimer.singleShot(1000, QApplication.instance().quit)


class Overlay(QWidget):
    def __init__(self, media, stats_path, duration_s):
        super().__init__()
        self.setWindowTitle("P01 T-02 C")
        self.setFixedSize(1920, 1080)
        self.setAttribute(Qt.WidgetAttribute.WA_OpaquePaintEvent)
        self._stats_path = stats_path

        # Schachbrett 1920x1080 einmal vorberechnen
        self._checker = QPixmap(1920, 1080)
        cp = QPainter(self._checker)
        light, dark = QColor(255, 255, 255), QColor(204, 204, 204)
        for y in range(0, 1080, 16):
            for x in range(0, 1920, 16):
                cp.fillRect(x, y, 16, 16, dark if (x // 16 + y // 16) % 2 else light)
        cp.end()

        self._images = [QImage(f"{media}/bild{i}.png") for i in range(1, 6)]
        self._frames = {"video": None, "alpha": None}
        self._times = {"frames": [], "video": [], "alpha": []}
        self._info = {}
        self._players = [self._make_player(f"{media}/{k}.webm", k) for k in ("video", "alpha")]

        self._font = QFont("DejaVu Sans")
        self._font.setPixelSize(64)
        self._font.setBold(True)

        self._t0 = time.monotonic_ns()
        self._next = self._t0
        self._schedule_next()
        QTimer.singleShot(duration_s * 1000, self._finish)

    def _make_player(self, path, key):
        player = QMediaPlayer(self)
        sink = QVideoSink(self)
        player.setVideoSink(sink)
        player.setLoops(QMediaPlayer.Loops.Infinite)

        def on_frame(frame):
            self._times[key].append(epoch_ms())
            if f"{key}_pixel_format" not in self._info:
                self._info[f"{key}_pixel_format"] = QVideoFrameFormat.pixelFormatToString(frame.pixelFormat())
            img = frame.toImage()
            self._info.setdefault(f"{key}_image_format", img.format().value)
            self._frames[key] = img

        def on_error(_err, msg):
            self._info[f"{key}_error"] = msg

        sink.videoFrameChanged.connect(on_frame)
        player.errorOccurred.connect(on_error)
        player.setSource(QUrl.fromLocalFile(path))
        player.play()
        return player, sink

    def paintEvent(self, _e):
        self._times["frames"].append(epoch_ms())
        t = (time.monotonic_ns() - self._t0) / 1e9

        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.drawPixmap(0, 0, self._checker)

        for i, img in enumerate(self._images):
            p.drawImage(QPoint(60 + 300 * i, 780), img)
        if self._frames["video"] is not None:
            p.drawImage(QRect(60, 60, 960, 540), self._frames["video"])
        if self._frames["alpha"] is not None:
            p.drawImage(QRect(1340, 480, 512, 512), self._frames["alpha"])

        float_y = 12.0 * math.sin(2 * math.pi * t / 3.0)
        breath = 1.0 + 0.06 * math.sin(2 * math.pi * t / 2.5)
        self._draw_text(p, "Schweben", QPointF(1100, 140 + float_y), 1.0)
        self._draw_text(p, "Atmen", QPointF(1100, 280), breath)
        self._draw_text(p, "DoNs Studio", QPointF(1100, 420 + float_y), breath)
        p.end()

    def _draw_text(self, p, s, pos, scale):
        p.save()
        p.translate(pos)
        p.scale(scale, scale)
        path = QPainterPath()
        path.addText(0, 0, self._font, s)
        p.setPen(QPen(QColor(0, 0, 0, 200), 6))
        p.drawPath(path)
        p.fillPath(path, QColor(255, 255, 255))
        p.restore()

    # Feste Bildzeitpunkte alle 1/30 s, ohne Aufsummieren von Fehlern
    def _schedule_next(self):
        self._next += 1_000_000_000 // 30
        wait_ms = max(0, (self._next - time.monotonic_ns()) // 1_000_000)
        QTimer.singleShot(int(wait_ms), Qt.TimerType.PreciseTimer, self._tick)

    def _tick(self):
        self.update()
        self._schedule_next()

    def _finish(self):
        data = {
            "variant": "C",
            "frames": self._times["frames"],
            "video_frames": self._times["video"],
            "alpha_frames": self._times["alpha"],
            "info": self._info,
        }
        with open(self._stats_path, "w") as f:
            json.dump(data, f)
        QApplication.instance().quit()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", default="t01", choices=["t01", "t02"])
    ap.add_argument("--media")
    ap.add_argument("--stats")
    ap.add_argument("--duration", type=int, default=70)
    args = ap.parse_args()

    app = QApplication(sys.argv[:1])
    if args.mode == "t02":
        w = Overlay(args.media, args.stats, args.duration)
        w.showFullScreen()
    else:
        w = StartWindow()
        w.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
