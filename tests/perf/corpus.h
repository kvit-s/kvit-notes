// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef KVIT_TESTS_PERF_CORPUS_H
#define KVIT_TESTS_PERF_CORPUS_H

#include <QString>

namespace PerfCorpus {

struct DocumentFixture {
    QString markdown;
    int blocks = 0;
    int words = 0;
    int headings = 0;
};

DocumentFixture warAndPeace();
DocumentFixture warAndPeaceLiveSized();
DocumentFixture warAndPeaceSynth();
DocumentFixture headings2K();
DocumentFixture list5K();
DocumentFixture mixed100();

int writeVault10K(const QString &rootPath);
int writeSingleNote(const QString &rootPath,
                    const QString &relPath,
                    const QString &markdown);

int countedBlocks(const QString &markdown);
int countedWords(const QString &markdown);
int countedHeadings(const QString &markdown);

} // namespace PerfCorpus

#endif // KVIT_TESTS_PERF_CORPUS_H
