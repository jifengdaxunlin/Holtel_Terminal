#!/usr/bin/env python3
"""Scale mainwindow.ui authored at 1024x600 down to 800x480.

The previous 1024x600 size still clipped on the 7-inch X6818 panel.
This script performs targeted, widget-aware replacements so the layout
fits 800x480 without overflowing on the right or bottom edges.
"""

import re
from pathlib import Path

UI = Path(__file__).parent / "mainwindow.ui"
text = UI.read_text(encoding="utf-8")


def repl_widget_prop(widget_name, prop_name, old_wh, new_wh):
    """Replace a <size> inside a specific widget's specific property."""
    global text
    pattern = rf'(<widget class="[^"]+" name="{widget_name}">.*?<property name="{prop_name}">\s*<size>)({old_wh})(</size>\s*</property>)'
    # Use DOTALL and only replace the first occurrence to avoid greed issues.
    # For simplicity we do a targeted string replace within the widget block.
    start = text.find(f'<widget class="QFrame" name="{widget_name}">')
    if start == -1:
        return
    # find the property inside this widget
    prop_start = text.find(f'<property name="{prop_name}">', start)
    if prop_start == -1:
        return
    size_start = text.find("<size>", prop_start)
    size_end = text.find("</size>", size_start)
    block = text[size_start:size_end + 7]
    if old_wh in block:
        text = text[:size_start] + block.replace(old_wh, new_wh, 1) + text[size_end + 7:]


# Main window geometry
repl_widget_prop("MainWindow", "geometry",
                 "<width>1024</width>\n    <height>600</height>",
                 "<width>800</width>\n    <height>480</height>")

# Sidebar width 100 -> 70
for prop in ("minimumSize", "maximumSize"):
    repl_widget_prop("sidebar", prop,
                     "<width>100</width><height>0</height>",
                     "<width>70</width><height>0</height>")
    repl_widget_prop("sidebar", prop,
                     "<width>100</width><height>16777215</height>",
                     "<width>70</width><height>16777215</height>")

# Top bar height 70 -> 50
for prop in ("minimumSize", "maximumSize"):
    repl_widget_prop("topBar", prop,
                     "<width>0</width><height>70</height>",
                     "<width>0</width><height>50</height>")

# Logo label 60x60 -> 40x40
for prop in ("minimumSize", "maximumSize"):
    repl_widget_prop("logoLabel", prop,
                     "<width>60</width><height>60</height>",
                     "<width>40</width><height>40</height>")

# Navigation buttons height 76 -> 52
for btn in ("navHome", "navRoom", "navCheckin", "navService", "navSettings"):
    start = text.find(f'<widget class="QToolButton" name="{btn}">')
    if start == -1:
        continue
    prop_start = text.find('<property name="minimumSize">', start)
    size_start = text.find("<size>", prop_start)
    size_end = text.find("</size>", size_start)
    block = text[size_start:size_end + 7]
    if "<height>76</height>" in block:
        text = text[:size_start] + block.replace("<height>76</height>", "<height>52</height>", 1) + text[size_end + 7:]

# Home page: card row spacing 16 -> 10
text = text.replace('<layout class="QHBoxLayout" name="homeRow1">\n            <property name="spacing"><number>16</number>', '<layout class="QHBoxLayout" name="homeRow1">\n            <property name="spacing"><number>10</number>')

# Room card height 260 -> 180
repl_widget_prop("roomCard", "minimumSize",
                 "<width>0</width><height>260</height>",
                 "<width>0</width><height>180</height>")

# Music card
repl_widget_prop("musicCard", "minimumSize",
                 "<width>260</width><height>260</height>",
                 "<width>170</width><height>180</height>")
repl_widget_prop("musicCard", "maximumSize",
                 "<width>280</width><height>16777215</height>",
                 "<width>190</width><height>16777215</height>")

# AC card
repl_widget_prop("acCard", "minimumSize",
                 "<width>220</width><height>260</height>",
                 "<width>140</width><height>180</height>")
repl_widget_prop("acCard", "maximumSize",
                 "<width>240</width><height>16777215</height>",
                 "<width>160</width><height>16777215</height>")

# Sensor boxes 70x70 -> 50x50
for sensor in ("sensorTemp", "sensorHumi", "sensorCo2", "sensorPm", "sensorHcho", "sensorIon"):
    for prop in ("minimumSize", "maximumSize"):
        repl_widget_prop(sensor, prop,
                         "<width>70</width><height>70</height>",
                         "<width>50</width><height>50</height>")

# Music central icon 90x90 -> 60x60
for prop in ("minimumSize", "maximumSize"):
    repl_widget_prop("musicIcon", prop,
                     "<width>90</width><height>90</height>",
                     "<width>60</width><height>60</height>")

# Music control buttons 40/48 -> 32/40
for btn, old, new in [("btnPrev", "40", "32"), ("btnNext", "40", "32"), ("btnPlay", "48", "40")]:
    start = text.find(f'<widget class="QPushButton" name="{btn}">')
    if start == -1:
        continue
    prop_start = text.find('<property name="minimumSize">', start)
    size_start = text.find("<size>", prop_start)
    size_end = text.find("</size>", size_start)
    block = text[size_start:size_end + 7]
    if f"<width>{old}</width><height>{old}</height>" in block:
        text = text[:size_start] + block.replace(f"<width>{old}</width><height>{old}</height>", f"<width>{new}</width><height>{new}</height>", 1) + text[size_end + 7:]

# Home row 2 / 3 spacing and big buttons
for row in ("homeRow2", "homeRow3"):
    text = text.replace(f'<layout class="QHBoxLayout" name="{row}">\n            <property name="spacing"><number>16</number>', f'<layout class="QHBoxLayout" name="{row}">\n            <property name="spacing"><number>10</number>')

# All bigBtn minimum height 90 -> 55
# These are inside QPushButton widgets with objectName bigBtn; target by name="btnDnd" etc.
for btn in ("btnDnd", "btnClean", "btnDoor", "btnLight", "btnScene", "btnCall", "btnService", "btnInfo"):
    start = text.find(f'<widget class="QPushButton" name="{btn}">')
    if start == -1:
        continue
    prop_start = text.find('<property name="minimumSize">', start)
    size_start = text.find("<size>", prop_start)
    size_end = text.find("</size>", size_start)
    block = text[size_start:size_end + 7]
    if "<height>90</height>" in block:
        text = text[:size_start] + block.replace("<height>90</height>", "<height>55</height>", 1) + text[size_end + 7:]

# Home layout margins 20/16 -> 10/10
text = text.replace('<layout class="QVBoxLayout" name="homeLayout">\n          <property name="spacing"><number>16</number></property>\n          <property name="leftMargin"><number>20</number></property>\n          <property name="topMargin"><number>16</number></property>\n          <property name="rightMargin"><number>20</number></property>\n          <property name="bottomMargin"><number>16</number></property>',
                    '<layout class="QVBoxLayout" name="homeLayout">\n          <property name="spacing"><number>10</number></property>\n          <property name="leftMargin"><number>10</number></property>\n          <property name="topMargin"><number>10</number></property>\n          <property name="rightMargin"><number>10</number></property>\n          <property name="bottomMargin"><number>10</number></property>')

# Check-in page cards
repl_widget_prop("faceCard", "minimumSize",
                 "<width>360</width><height>380</height>",
                 "<width>260</width><height>260</height>")
repl_widget_prop("idCard", "minimumSize",
                 "<width>360</width><height>380</height>",
                 "<width>260</width><height>260</height>")
repl_widget_prop("resultCard", "minimumSize",
                 "<width>340</width><height>180</height>",
                 "<width>240</width><height>130</height>")
repl_widget_prop("roomInfoCard", "minimumSize",
                 "<width>340</width><height>180</height>",
                 "<width>240</width><height>130</height>")

# Check-in action buttons height 56 -> 40
for btn in ("btnReadId", "btnStartFace", "btnConfirmCheckin", "btnPrint", "btnCardAuth"):
    start = text.find(f'<widget class="QPushButton" name="{btn}">')
    if start == -1:
        continue
    prop_start = text.find('<property name="minimumSize">', start)
    size_start = text.find("<size>", prop_start)
    size_end = text.find("</size>", size_start)
    block = text[size_start:size_end + 7]
    if "<height>56</height>" in block:
        text = text[:size_start] + block.replace("<height>56</height>", "<height>40</height>", 1) + text[size_end + 7:]

# Check-in layout margins 20/16 -> 10/10
text = text.replace('<layout class="QVBoxLayout" name="checkinLayout">\n          <property name="spacing"><number>16</number></property>\n          <property name="leftMargin"><number>20</number></property>\n          <property name="topMargin"><number>16</number></property>\n          <property name="rightMargin"><number>20</number></property>\n          <property name="bottomMargin"><number>16</number></property>',
                    '<layout class="QVBoxLayout" name="checkinLayout">\n          <property name="spacing"><number>10</number></property>\n          <property name="leftMargin"><number>10</number></property>\n          <property name="topMargin"><number>10</number></property>\n          <property name="rightMargin"><number>10</number></property>\n          <property name="bottomMargin"><number>10</number></property>')

# Check-in actions spacing 16 -> 10
text = text.replace('<layout class="QHBoxLayout" name="checkinActions">\n            <property name="spacing"><number>16</number>', '<layout class="QHBoxLayout" name="checkinActions">\n            <property name="spacing"><number>10</number>')

# Reduce some font sizes in style sheet block
text = text.replace('font-size: 26px;', 'font-size: 18px;')       # logo
# Top bar title 20 -> 17, subtitle 13 -> 11, info 14 -> 12, time 26 -> 20
text = text.replace('QLabel#topTitle { color: #ffffff; font-size: 20px; font-weight: bold; }',
                    'QLabel#topTitle { color: #ffffff; font-size: 17px; font-weight: bold; }')
text = text.replace('QLabel#topSubTitle { color: #8aa4c8; font-size: 13px; }',
                    'QLabel#topSubTitle { color: #8aa4c8; font-size: 11px; }')
text = text.replace('QLabel#topInfo { color: #8aa4c8; font-size: 14px; }',
                    'QLabel#topInfo { color: #8aa4c8; font-size: 12px; }')
text = text.replace('QLabel#topTime { color: #00d4ff; font-size: 26px; font-weight: bold; }',
                    'QLabel#topTime { color: #00d4ff; font-size: 20px; font-weight: bold; }')
text = text.replace('font-size: 13px; }', 'font-size: 11px; }', 1)  # navBtn
# Big button font 16 -> 13
text = text.replace('font-size: 16px;\n    text-align: left;', 'font-size: 13px;\n    text-align: left;')
# Section card title 18 -> 15, sub 13 -> 11
text = text.replace('QLabel#roomCardTitle { color: #ffffff; font-size: 18px; font-weight: bold; }',
                    'QLabel#roomCardTitle { color: #ffffff; font-size: 15px; font-weight: bold; }')
text = text.replace('QLabel#roomCardSub { color: #8aa4c8; font-size: 13px; }',
                    'QLabel#roomCardSub { color: #8aa4c8; font-size: 11px; }')
text = text.replace('QLabel#musicTitle { color: #ffffff; font-size: 16px; font-weight: bold; }',
                    'QLabel#musicTitle { color: #ffffff; font-size: 13px; font-weight: bold; }')
text = text.replace('QLabel#acTitle { color: #ffffff; font-size: 16px; font-weight: bold; }',
                    'QLabel#acTitle { color: #ffffff; font-size: 13px; font-weight: bold; }')
text = text.replace('QLabel#acTempBig { color: #00d4ff; font-size: 32px; font-weight: bold; }',
                    'QLabel#acTempBig { color: #00d4ff; font-size: 22px; font-weight: bold; }')
text = text.replace('font-size: 15px; font-weight: bold;', 'font-size: 12px; font-weight: bold;')  # sensor values
# Music labels
text = text.replace('color: #ffffff; font-size: 14px;', 'color: #ffffff; font-size: 12px;', 1)
text = text.replace('color: #8aa4c8; font-size: 12px;', 'color: #8aa4c8; font-size: 10px;', 1)
# Check-in header
text = text.replace('color: #ffffff; font-size: 22px; font-weight: bold;',
                    'color: #ffffff; font-size: 16px; font-weight: bold;')
# Face/id titles
text = text.replace('QLabel#faceTitle { color: #ffffff; font-size: 16px; font-weight: bold; }',
                    'QLabel#faceTitle { color: #ffffff; font-size: 13px; font-weight: bold; }')
text = text.replace('QLabel#idTitle { color: #ffffff; font-size: 16px; font-weight: bold; }',
                    'QLabel#idTitle { color: #ffffff; font-size: 13px; font-weight: bold; }')
text = text.replace('font-size: 13px; }', 'font-size: 11px; }')  # id labels, but careful already used above
# Result / room info
text = text.replace('QLabel#resultTitle { color: #ffffff; font-size: 16px; font-weight: bold; }',
                    'QLabel#resultTitle { color: #ffffff; font-size: 13px; font-weight: bold; }')
text = text.replace('QLabel#roomInfoTitle { color: #ffffff; font-size: 16px; font-weight: bold; }',
                    'QLabel#roomInfoTitle { color: #ffffff; font-size: 13px; font-weight: bold; }')
text = text.replace('font-size: 28px; font-weight: bold;', 'font-size: 20px; font-weight: bold;')  # similarity value
# Action buttons font 15 -> 13
text = text.replace('font-size: 15px;\n    text-align: center;', 'font-size: 13px;\n    text-align: center;')
text = text.replace('font-size: 15px;\n    font-weight: bold;', 'font-size: 13px;\n    font-weight: bold;')

# roomImage min-height 120 -> 85
repl_widget_prop("roomImage", "minimumSize",
                 "<width>0</width><height>120</height>",
                 "<width>0</width><height>85</height>")
# facePreview min-height 260 -> 170
repl_widget_prop("facePreview", "minimumSize",
                 "<width>0</width><height>260</height>",
                 "<width>0</width><height>170</height>")
# idImage min-height 160 -> 100
repl_widget_prop("idImage", "minimumSize",
                 "<width>0</width><height>160</height>",
                 "<width>0</width><height>100</height>")

# Top bar layout margins 25 -> 15
text = text.replace('<layout class="QHBoxLayout" name="topBarLayout">\n         <property name="leftMargin"><number>25</number></property>\n         <property name="rightMargin"><number>25</number></property>',
                    '<layout class="QHBoxLayout" name="topBarLayout">\n         <property name="leftMargin"><number>15</number></property>\n         <property name="rightMargin"><number>15</number></property>')

UI.write_text(text, encoding="utf-8")
print("mainwindow.ui scaled to 800x480")
