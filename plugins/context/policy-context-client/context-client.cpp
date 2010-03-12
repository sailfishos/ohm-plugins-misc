#include <QtCore/QCoreApplication>
#include "context-tracker.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    ContextTracker t(&app);

    return app.exec();
}
