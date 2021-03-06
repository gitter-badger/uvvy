#pragma once

#include <QTemporaryFile>

#include "action.h"
#include "opaque.h"
#include "file.h"

/**
 * Action: update a local file or directory tree from the network
 */
class Update : public Action
{
    Q_OBJECT

    class FileReader : public AbstractOpaqueReader
    {
        Update *up;
        QTemporaryFile tmp;

        virtual void gotData2(const QByteArray &ohash,
                    qint64 ofs, qint64 recno,
                    const QByteArray &data, int nrecs);
        void gotMetaData(const QByteArray &ohash,
                    const QByteArray &chunkdata,
                    qint64 ofs, qint64 recno,
                    qint64 size, int nrecs);
        virtual void noData2(const QByteArray &ohash,
                    qint64 ofs, qint64 recno,
                    qint64 size, int nrecs);
        void readDone();

        void error(const QString &msg);

    public:
        FileReader(Update *update);
        ~FileReader();

        void start();
    };

    class DirReader : public AbstractDirReader
    {
        Update *up;
        bool dirdone;

        virtual void gotData(const QByteArray &ohash, const QByteArray &data);
        void gotEntries(int pos, qint64 recno, const QList<FileInfo> &ents);
        void noEntries(int pos, qint64 recno, int nents);
        void readDone();

    public:
        DirReader(Update *update);

        void start();
        void check();
    };

    const FileInfo info;
    const QString path;
    qint64 totbytes, gotbytes;
    int nerrors;
    FileReader *fr;
    DirReader *dr;

public:
    Update(QObject *parent, const FileInfo &info, const QString &path);

    virtual void start();

private:
    void report();

private slots:
    void subChanged();
};
