#include <contextproperty.h>

class ContextTracker: public QObject
{
    Q_OBJECT
public:
    ContextTracker(QObject *parent);
    ~ContextTracker();
    void initialize();
private slots:
    void audioRouteChanged();
    void audioSourceChanged();
    void videoRouteChanged();
//    onCall();
//    onCallAudioType();
//    modeChanged();

private:
    ContextProperty *cpAudioRoute;
    ContextProperty *cpAudioSource;
    ContextProperty *cpVideoRoute;

//    ContextProperty *cpCall;
//    ContextProperty *cpCallAudioType;
//    ContextProperty *cpMode;
};
