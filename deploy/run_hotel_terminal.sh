#!/bin/sh
# =============================================================
# HotelTerminal 开机自启 + 崩溃守护（X6818 / buildroot / Qt 5.4.1）
# 用法：
#   1) 把本脚本放到板子 /opt/Qt_program/run_hotel_terminal.sh 并 chmod +x
#   2) 手动测试： sh /opt/Qt_program/run_hotel_terminal.sh &
#   3) 开机自启：在 /etc/init.d/rcS 末尾追加一行：
#        /opt/Qt_program/run_hotel_terminal.sh &
#      （或放入 /etc/init.d/S99hotel_terminal 链接到本脚本）
#   4) 停止：先创建 /opt/Qt_program/.stop 再退出程序，或直接 kill 本脚本
# =============================================================

APP_DIR="/opt/Qt_program"
APP="$APP_DIR/hotel_terminal"
LOG="$APP_DIR/run.log"
RESTART_DELAY=3          # 崩溃后等待秒数（防止快速循环拉起）

# ---- 运行环境（与手动启动保持一致；板子上已设置的环境变量不覆盖）----
cd "$APP_DIR" || exit 1
[ -z "$QT_QPA_PLATFORM" ] && export QT_QPA_PLATFORM="linuxfb:fb=/dev/fb0"
[ -z "$TSLIB_TSDEVICE" ]  && export TSLIB_TSDEVICE="/dev/input/event0"
# 如二进制提示缺 Qt 库，取消下行注释并按板子实际路径调整：
# export LD_LIBRARY_PATH="/usr/lib/qt5lib:$LD_LIBRARY_PATH"
export QT_LOGGING_RULES="*.debug=false"    # 关 debug 日志，减少刷屏

# 单实例：已有一个在跑就不重复拉起（rcS 重跑/手动误触双保险）
if [ -f "$APP_DIR/hotel_terminal.pid" ] && kill -0 "$(cat "$APP_DIR/hotel_terminal.pid")" 2>/dev/null; then
    exit 0
fi

while true; do
    # 软停止开关：存在 .stop 文件则退出守护
    if [ -f "$APP_DIR/.stop" ]; then
        rm -f "$APP_DIR/.stop"
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] watchdog stopped by .stop" >> "$LOG"
        break
    fi
    # 日志超过 1MB 直接清空（嵌入式板子经不起无限增长）
    if [ -f "$LOG" ] && [ "$(wc -c < "$LOG")" -gt 1048576 ]; then
        : > "$LOG"
    fi
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] HotelTerminal starting..." >> "$LOG"
    "$APP" >> "$LOG" 2>&1 &
    APP_PID=$!
    echo "$APP_PID" > "$APP_DIR/hotel_terminal.pid"
    wait "$APP_PID"
    CODE=$?
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] HotelTerminal exited code=$CODE, restart in ${RESTART_DELAY}s" >> "$LOG"
    sleep "$RESTART_DELAY"
done
