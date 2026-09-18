// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Paths a test needs, resolved at runtime rather than compiled in.
//
// The rule these exist for: no absolute path should reach a compile command.
// A compiler cache such as ccache decides whether it has compiled a file
// before by hashing the compiler, the whole argument list and the contents of
// every file the compile reads. It rewrites the arguments it recognises as
// paths -- -I, -isystem, -include, the input file -- to paths relative to the
// working directory before hashing, so that two checkouts of the same commit
// at different paths produce one cache entry rather than two. It cannot
// rewrite a path inside a macro value, because nothing tells it that one is a
// path. So one such macro makes every translation unit that sees it look like
// a different compile in a second checkout.
//
// tests/CMakeLists.txt sets these through CTest's ENVIRONMENT property, the
// same way it already sets QT_QPA_PLATFORM. Run by hand with nothing set, each
// falls back to a path relative to the test binary's own directory, so no test
// needs the environment in order to run.
#ifndef TESTPATHS_H
#define TESTPATHS_H

#include <QCoreApplication>
#include <QDir>
#include <QString>

namespace KvitTestPaths {

// The repository root.
//
// The fallback is two directories above the binary, which is the checkout for
// the layout build.sh produces: <checkout>/build/tests/<binary>.
inline QString sourceRoot()
{
    const QString named = qEnvironmentVariable("KVIT_SOURCE_ROOT");
    if (!named.isEmpty())
        return named;
    return QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("../.."));
}

// The committed fixture directory the media-block and corpus tests read.
inline QString fixtures()
{
    const QString named = qEnvironmentVariable("KVIT_TEST_FIXTURES");
    if (!named.isEmpty())
        return named;
    return QDir(sourceRoot()).filePath(QStringLiteral("tests/fixtures"));
}

} // namespace KvitTestPaths

#endif // TESTPATHS_H
