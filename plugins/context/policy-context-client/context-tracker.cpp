#include <QDebug>
#include "context-tracker.h"

ContextTracker::ContextTracker(QObject *parent): QObject(parent)
{
    cpAudioRoute = new ContextProperty("com.nokia.policy.audio_route", this);
    cpAudioSource = new ContextProperty("com.nokia.policy.audio_source", this);
    cpVideoRoute = new ContextProperty("com.nokia.policy.video_route", this);
//    cpCall = new ContextProperty("com.nokia.policy.audio_route", this);
//    cpCallAudioType = new ContextProperty("com.nokia.policy.audio_route", this);
//    cpMode = new ContextProperty("com.nokia.policy.audio_route", this);
    QObject::connect(cpAudioRoute, SIGNAL(valueChanged()),this, SLOT(audioRouteChanged()));
    QObject::connect(cpAudioSource, SIGNAL(valueChanged()),this, SLOT(audioSourceChanged()));
    QObject::connect(cpVideoRoute, SIGNAL(valueChanged()),this, SLOT(videoRouteChanged()));
}

ContextTracker::~ContextTracker()
{}

void ContextTracker::audioRouteChanged()
{
    qWarning() << "Audio is now routed to " << cpAudioRoute->value();
}

void ContextTracker::audioSourceChanged()
{
    qWarning() << "Audio source is now " << cpAudioSource->value();
}

void ContextTracker::videoRouteChanged()
{
    qWarning() << "Video is now routed to " << cpVideoRoute->value();
}

//ContextTracker::modeChanged();
//ContextTracker::onCall();
