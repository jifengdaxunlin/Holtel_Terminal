#ifndef SYSTEMINTEGRATION_H
#define SYSTEMINTEGRATION_H

#include <QObject>

/*
 * Day1 placeholder for the new IoTDA / WiFi / Face-Recognition integration.
 *
 * Currently it does nothing on the UI side. Day 2 will wire:
 *   - huawei::Iot  -> Shadow polling + control message dispatch
 *   - WifiManager  -> settings page scan / connect
 *   - CameraThread + OpenCV -> check-in page face preview
 *
 * The class exists so mainwindow.cpp has a single #include to drop in a new
 * module without juggling many slots / pointers at once.
 */
class SystemIntegration : public QObject
{
    Q_OBJECT
public:
    explicit SystemIntegration(QObject *parent = 0);
    ~SystemIntegration();

    bool wifiAvailable()  const { return m_wifiReady;  }
    bool cameraAvailable() const { return m_cameraReady; }

private:
    bool m_wifiReady    = false;   // true once WifiManager backend resolves
    bool m_cameraReady  = false;   // true once /dev/video0 enumerates
};

#endif // SYSTEMINTEGRATION_H
