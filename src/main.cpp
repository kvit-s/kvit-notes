// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The open editor's launcher. Everything it used to do now lives in the core
// library — AppContext composes the editor, KvitApplication owns the QML
// engine — so that a superset binary built from the core plus a premium
// module supplies its own main() without copying any of that wiring.
//
// The premium module is compiled only with -DKVIT_AGENT=ON and lives entirely
// under src/agent/, so splitting the tree into a public repo and a private one
// is a file move plus the deletion of the two guarded fragments below.

#include <QApplication>

#include <cstring>

#include "extensionregistry.h"
#include "kvitapplication.h"
#include "mathrenderer.h"

#ifdef KVIT_AGENT
#include "agent/agentmodule.h"
#endif

int main(int argc, char *argv[])
{
    // Headless packaging probe: verifies the installed math resources
    // resolve and render from wherever the package was installed.
    // Handled before QApplication because it builds its own offscreen one.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--math-selftest") == 0)
            return MathRenderer::runSelfTest();
    }

    KvitApplication::applyPlatformWorkarounds();
    QApplication app(argc, argv);

    KvitApplication kvit(app);

#ifdef KVIT_AGENT
    KvitAgent::install(*kvit.context().extensions());
#endif

    if (!kvit.start(app.arguments()))
        return -1;

    return app.exec();
}
