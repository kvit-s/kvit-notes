// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef WINDOWROUTER_H
#define WINDOWROUTER_H

#include <QString>

class AppContext;

// How a window's open actions reach the process-wide window registry.
//
// A menu item or dialog inside a window asks its AppContext to open a vault or
// file; AppContext forwards the request through this interface rather than
// depending on the registry directly. WindowRegistry implements it. When no
// router is installed — a single-composition test, which has no registry —
// AppContext falls back to switching itself in place, exactly as it behaved
// before the multi-window split.
class WindowRouter
{
public:
    virtual ~WindowRouter() = default;

    // Open `path` as a vault in the window that asked (`requester`), switching
    // it in place — unless that vault is already open in another window, in
    // which case raise that window and leave the requester unchanged.
    virtual void openVaultInWindow(AppContext *requester, const QString &path) = 0;

    // Open `path` as a vault in a new window, raising an existing one if that
    // vault is already open.
    virtual void openVaultInNewWindow(const QString &path) = 0;

    // Open a loose file `path` in its own single-file window, raising an
    // existing one if that file is already open.
    virtual void openFileInNewWindow(const QString &path) = 0;
};

#endif // WINDOWROUTER_H
