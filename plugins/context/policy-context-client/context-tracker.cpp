#include <QDebug>
#include "context-tracker.h"

ContextTracker::ContextTracker(QObject *parent): QObject(parent)
{
    cpAudioRoute = new ContextProperty("com.nokia.policy.audio_route", this);
    cpAudioSource = new ContextProperty("com.nokia.policy.audio_source", this);
//    cpCall = new ContextProperty("com.nokia.policy.audio_route", this);
//    cpCallAudioType = new ContextProperty("com.nokia.policy.audio_route", this);
//    cpMode = new ContextProperty("com.nokia.policy.audio_route", this);
    QObject::connect(cpAudioRoute, SIGNAL(valueChanged()),this, SLOT(audioRouteChanged()));
    QObject::connect(cpAudioSource, SIGNAL(valueChanged()),this, SLOT(audioSourceChanged()));
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

//ContextTracker::modeChanged();
//ContextTracker::onCall();
