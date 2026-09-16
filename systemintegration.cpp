#include "systemintegration.h"

SystemIntegration::SystemIntegration(QObject *parent)
    : QObject(parent)
{
    // Day 1: do not touch hardware; just confirm the modules load cleanly.
    m_wifiReady   = false;
    m_cameraReady = false;
}

SystemIntegration::~SystemIntegration() = default;
