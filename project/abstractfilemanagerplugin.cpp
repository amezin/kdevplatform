/***************************************************************************
 *   This file is part of KDevelop                                         *
 *   Copyright 2010-2012 Milian Wolff <mail@milianw.de>                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/

#include "abstractfilemanagerplugin.h"

#include "filemanagerlistjob.h"
#include "projectmodel.h"
#include "helper.h"

#include <QHashIterator>
#include <QFileInfo>
#include <QApplication>

#include <KMessageBox>
#include <KLocalizedString>
#include <KDirWatch>

#include <interfaces/iproject.h>
#include <interfaces/icore.h>
#include <interfaces/iprojectcontroller.h>
#include <serialization/indexedstring.h>

#include "projectfiltermanager.h"
#include "debug.h"

#define ifDebug(x)

using namespace KDevelop;

//BEGIN Helper

namespace {

/**
 * Returns the parent folder item for a given item or the project root item if there is no parent.
 */
ProjectFolderItem* getParentFolder(ProjectBaseItem* item)
{
    if ( item->parent() ) {
        return static_cast<ProjectFolderItem*>(item->parent());
    } else {
        return item->project()->projectItem();
    }
}

}

//END Helper

//BEGIN Private

class KDevelop::AbstractFileManagerPluginPrivate
{
public:
    explicit AbstractFileManagerPluginPrivate(AbstractFileManagerPlugin* qq)
        : q(qq)
    {
    }

    AbstractFileManagerPlugin* q;

    /**
     * The just returned must be started in one way or another for this method
     * to have any affect. The job will then auto-delete itself upon completion.
     */
    Q_REQUIRED_RESULT KIO::Job* eventuallyReadFolder(ProjectFolderItem* item);
    void addJobItems(FileManagerListJob* job,
                     ProjectFolderItem* baseItem,
                     const KIO::UDSEntryList& entries);

    void deleted(const QString &path);
    void created(const QString &path);

    void projectClosing(IProject* project);
    void jobFinished(KJob* job);

    /// Stops watching the given folder for changes, only useful for local files.
    void stopWatcher(ProjectFolderItem* folder);
    /// Continues watching the given folder for changes.
    void continueWatcher(ProjectFolderItem* folder);
    /// Common renaming function.
    bool rename(ProjectBaseItem* item, const Path& newPath);

    void removeFolder(ProjectFolderItem* folder);

    QHash<IProject*, KDirWatch*> m_watchers;
    QHash<IProject*, QList<FileManagerListJob*> > m_projectJobs;
    QVector<QString> m_stoppedFolders;
    ProjectFilterManager m_filters;
};

void AbstractFileManagerPluginPrivate::projectClosing(IProject* project)
{
    if ( m_projectJobs.contains(project) ) {
        // make sure the import job does not live longer than the project
        // see also addLotsOfFiles test
        foreach( FileManagerListJob* job, m_projectJobs[project] ) {
            qCDebug(FILEMANAGER) << "killing project job:" << job;
            job->abort();
        }
        m_projectJobs.remove(project);
    }
    delete m_watchers.take(project);
    m_filters.remove(project);
}

KIO::Job* AbstractFileManagerPluginPrivate::eventuallyReadFolder(ProjectFolderItem* item)
{
    FileManagerListJob* listJob = new FileManagerListJob( item );
    m_projectJobs[ item->project() ] << listJob;
    qCDebug(FILEMANAGER) << "adding job" << listJob << item << item->path() << "for project" << item->project();

    q->connect( listJob, &FileManagerListJob::finished,
                q, [&] (KJob* job) { jobFinished(job); } );

    q->connect( listJob, &FileManagerListJob::entries,
                q, [&] (FileManagerListJob* job, ProjectFolderItem* baseItem, const KIO::UDSEntryList& entries) {
                    addJobItems(job, baseItem, entries); } );

    return listJob;
}

void AbstractFileManagerPluginPrivate::jobFinished(KJob* job)
{
    FileManagerListJob* gmlJob = qobject_cast<FileManagerListJob*>(job);
    if (gmlJob) {
        ifDebug(qCDebug(FILEMANAGER) << job << gmlJob << gmlJob->item();)
        m_projectJobs[ gmlJob->item()->project() ].removeOne( gmlJob );
    } else {
        // job emitted its finished signal from its destructor
        // ensure we don't keep a dangling point in our list
        foreach (auto jobs, m_projectJobs) {
            if (jobs.removeOne(reinterpret_cast<FileManagerListJob*>(job))) {
                break;
            }
        }
    }
}

void AbstractFileManagerPluginPrivate::addJobItems(FileManagerListJob* job,
                                                     ProjectFolderItem* baseItem,
                                                     const KIO::UDSEntryList& entries)
{
    if ( entries.empty() ) {
        return;
    }

    qCDebug(FILEMANAGER) << "reading entries of" << baseItem->path();

    // build lists of valid files and folders with paths relative to the project folder
    Path::List files;
    Path::List folders;
    foreach ( const KIO::UDSEntry& entry, entries ) {
        QString name = entry.stringValue( KIO::UDSEntry::UDS_NAME );
        if (name == QLatin1String(".") || name == QLatin1String("..")) {
            continue;
        }

        Path path(baseItem->path(), name);

        if ( !q->isValid( path, entry.isDir(), baseItem->project() ) ) {
            continue;
        } else {
            if ( entry.isDir() ) {
                if( entry.isLink() ) {
                    const Path linkedPath = baseItem->path().cd(entry.stringValue( KIO::UDSEntry::UDS_LINK_DEST ));
                    // make sure we don't end in an infinite loop
                    if( linkedPath.isParentOf( baseItem->project()->path() ) ||
                        baseItem->project()->path().isParentOf( linkedPath ) ||
                        linkedPath == baseItem->project()->path() )
                    {
                        continue;
                    }
                }
                folders << path;
            } else {
                files << path;
            }
        }
    }

    ifDebug(qCDebug(FILEMANAGER) << "valid folders:" << folders;)
    ifDebug(qCDebug(FILEMANAGER) << "valid files:" << files;)

    // remove obsolete rows
    for ( int j = 0; j < baseItem->rowCount(); ++j ) {
        if ( ProjectFolderItem* f = baseItem->child(j)->folder() ) {
            // check if this is still a valid folder
            int index = folders.indexOf( f->path() );
            if ( index == -1 ) {
                // folder got removed or is now invalid
                removeFolder(f);
                --j;
            } else {
                // this folder already exists in the view
                folders.remove( index );
                // no need to add this item, but we still want to recurse into it
                job->addSubDir( f );
                emit q->reloadedFolderItem( f );
            }
        } else if ( ProjectFileItem* f =  baseItem->child(j)->file() ) {
            // check if this is still a valid file
            int index = files.indexOf( f->path() );
            if ( index == -1 ) {
                // file got removed or is now invalid
                ifDebug(qCDebug(FILEMANAGER) << "removing file:" << f << f->path();)
                baseItem->removeRow( j );
                --j;
            } else {
                // this file already exists in the view
                files.remove( index );
                emit q->reloadedFileItem( f );
            }
        }
    }

    // add new rows
    foreach ( const Path& path, files ) {
        ProjectFileItem* file = q->createFileItem( baseItem->project(), path, baseItem );
        if (file) {
            emit q->fileAdded( file );
        }
    }
    foreach ( const Path& path, folders ) {
        ProjectFolderItem* folder = q->createFolderItem( baseItem->project(), path, baseItem );
        if (folder) {
            emit q->folderAdded( folder );
            job->addSubDir( folder );
        }
    }
}

void AbstractFileManagerPluginPrivate::created(const QString& path_)
{
    qCDebug(FILEMANAGER) << "created:" << path_;
    QFileInfo info(path_);

    ///FIXME: share memory with parent
    const Path path(path_);
    const IndexedString indexedPath(path.pathOrUrl());
    const IndexedString indexedParent(path.parent().pathOrUrl());

    QHashIterator<IProject*, KDirWatch*> it(m_watchers);
    while (it.hasNext()) {
        const auto p = it.next().key();
        if ( !p->projectItem()->model() ) {
            // not yet finished with loading
            // FIXME: how should this be handled? see unit test
            continue;
        }
        if ( !q->isValid(path, info.isDir(), p) ) {
            continue;
        }
        if ( info.isDir() ) {
            bool found = false;
            foreach ( ProjectFolderItem* folder, p->foldersForPath(indexedPath) ) {
                // exists already in this project, happens e.g. when we restart the dirwatcher
                // or if we delete and remove folders consecutively https://bugs.kde.org/show_bug.cgi?id=260741
                qCDebug(FILEMANAGER) << "force reload of" << path << folder;
                auto job = eventuallyReadFolder( folder );
                job->start();
                found = true;
            }
            if ( found ) {
                continue;
            }
        } else if (!p->filesForPath(indexedPath).isEmpty()) {
            // also gets triggered for kate's backup files
            continue;
        }
        foreach ( ProjectFolderItem* parentItem, p->foldersForPath(indexedParent) ) {
            if ( info.isDir() ) {
                ProjectFolderItem* folder = q->createFolderItem( p, path, parentItem );
                if (folder) {
                    emit q->folderAdded( folder );
                    auto job = eventuallyReadFolder( folder );
                    job->start();
                }
            } else {
                ProjectFileItem* file = q->createFileItem( p, path, parentItem );
                if (file) {
                    emit q->fileAdded( file );
                }
            }
        }
    }
}

void AbstractFileManagerPluginPrivate::deleted(const QString& path_)
{
    if ( QFile::exists(path_) ) {
        // stopDirScan...
        return;
    }
    // ensure that the path is not inside a stopped folder
    foreach(const QString& folder, m_stoppedFolders) {
        if (path_.startsWith(folder)) {
            return;
        }
    }
    qCDebug(FILEMANAGER) << "deleted:" << path_;

    const Path path(QUrl::fromLocalFile(path_));
    const IndexedString indexed(path.pathOrUrl());

    QHashIterator<IProject*, KDirWatch*> it(m_watchers);
    while (it.hasNext()) {
        const auto p = it.next().key();
        if (path == p->path()) {
            KMessageBox::error(qApp->activeWindow(),
                               i18n("The base folder of project <b>%1</b>"
                                    " got deleted or moved outside of KDevelop.\n"
                                    "The project has to be closed.", p->name()),
                               i18n("Project Folder Deleted") );
            ICore::self()->projectController()->closeProject(p);
            continue;
        }
        if ( !p->projectItem()->model() ) {
            // not yet finished with loading
            // FIXME: how should this be handled? see unit test
            continue;
        }
        foreach ( ProjectFolderItem* item, p->foldersForPath(indexed) ) {
            removeFolder(item);
        }
        foreach ( ProjectFileItem* item, p->filesForPath(indexed) ) {
            emit q->fileRemoved(item);
            ifDebug(qCDebug(FILEMANAGER) << "removing file" << item;)
            item->parent()->removeRow(item->row());
        }
    }
}

bool AbstractFileManagerPluginPrivate::rename(ProjectBaseItem* item, const Path& newPath)
{
    if ( !q->isValid(newPath, true, item->project()) ) {
        int cancel = KMessageBox::warningContinueCancel( qApp->activeWindow(),
            i18n("You tried to rename '%1' to '%2', but the latter is filtered and will be hidden.\n"
                 "Do you want to continue?", item->text(), newPath.lastPathSegment()),
            QString(), KStandardGuiItem::cont(), KStandardGuiItem::cancel(), QStringLiteral("GenericManagerRenameToFiltered")
        );
        if ( cancel == KMessageBox::Cancel ) {
            return false;
        }
    }
    foreach ( ProjectFolderItem* parent, item->project()->foldersForPath(IndexedString(newPath.parent().pathOrUrl())) ) {
        if ( parent->folder() ) {
            stopWatcher(parent);
            const Path source = item->path();
            bool success = renameUrl( item->project(), source.toUrl(), newPath.toUrl() );
            if ( success ) {
                item->setPath( newPath );
                item->parent()->takeRow( item->row() );
                parent->appendRow( item );
                if (item->file()) {
                    emit q->fileRenamed(source, item->file());
                } else {
                    Q_ASSERT(item->folder());
                    emit q->folderRenamed(source, item->folder());
                }
            }
            continueWatcher(parent);
            return success;
        }
    }
    return false;
}

void AbstractFileManagerPluginPrivate::stopWatcher(ProjectFolderItem* folder)
{
    if ( !folder->path().isLocalFile() ) {
        return;
    }
    Q_ASSERT(m_watchers.contains(folder->project()));
    const QString path = folder->path().toLocalFile();
    m_watchers[folder->project()]->stopDirScan(path);
    m_stoppedFolders.append(path);
}

void AbstractFileManagerPluginPrivate::continueWatcher(ProjectFolderItem* folder)
{
    if ( !folder->path().isLocalFile() ) {
        return;
    }
    Q_ASSERT(m_watchers.contains(folder->project()));
    const QString path = folder->path().toLocalFile();
    m_watchers[folder->project()]->restartDirScan(path);
    const int idx = m_stoppedFolders.indexOf(path);
    if (idx != -1) {
        m_stoppedFolders.remove(idx);
    }
}

bool isChildItem(ProjectBaseItem* parent, ProjectBaseItem* child)
{
    do {
        if (child == parent) {
            return true;
        }
        child = child->parent();
    } while(child);
    return false;
}

void AbstractFileManagerPluginPrivate::removeFolder(ProjectFolderItem* folder)
{
    ifDebug(qCDebug(FILEMANAGER) << "removing folder:" << folder << folder->path();)
    foreach(FileManagerListJob* job, m_projectJobs[folder->project()]) {
        if (isChildItem(folder, job->item())) {
            qCDebug(FILEMANAGER) << "killing list job for removed folder" << job << folder->path();
            job->abort();
            Q_ASSERT(!m_projectJobs.value(folder->project()).contains(job));
        } else {
            job->removeSubDir(folder);
        }
    }
    folder->parent()->removeRow( folder->row() );
}

//END Private

//BEGIN Plugin

AbstractFileManagerPlugin::AbstractFileManagerPlugin( const QString& componentName,
                                                      QObject *parent,
                                                      const QVariantList & /*args*/ )
    : IProjectFileManager(),
      IPlugin( componentName, parent ),
      d(new AbstractFileManagerPluginPrivate(this))
{
    connect(core()->projectController(), &IProjectController::projectClosing,
            this, [&] (IProject* project) { d->projectClosing(project); });
}

AbstractFileManagerPlugin::~AbstractFileManagerPlugin() = default;

IProjectFileManager::Features AbstractFileManagerPlugin::features() const
{
    return Features( Folders | Files );
}

QList<ProjectFolderItem*> AbstractFileManagerPlugin::parse( ProjectFolderItem *item )
{
    // we are async, can't return anything here
    qCDebug(FILEMANAGER) << "note: parse will always return an empty list";
    Q_UNUSED(item);
    return QList<ProjectFolderItem*>();
}

ProjectFolderItem *AbstractFileManagerPlugin::import( IProject *project )
{
    ProjectFolderItem *projectRoot = createFolderItem( project, project->path(), nullptr );
    emit folderAdded( projectRoot );
    qCDebug(FILEMANAGER) << "imported new project" << project->name() << "at" << projectRoot->path();

    ///TODO: check if this works for remote files when something gets changed through another KDE app
    if ( project->path().isLocalFile() ) {
        d->m_watchers[project] = new KDirWatch( project );

        connect(d->m_watchers[project], &KDirWatch::created,
                this, [&] (const QString& path_) { d->created(path_); });
        connect(d->m_watchers[project], &KDirWatch::deleted,
                this, [&] (const QString& path_) { d->deleted(path_); });

        d->m_watchers[project]->addDir(project->path().toLocalFile(), KDirWatch::WatchSubDirs | KDirWatch:: WatchFiles );
    }

    d->m_filters.add(project);

    return projectRoot;
}

KJob* AbstractFileManagerPlugin::createImportJob(ProjectFolderItem* item)
{
    return d->eventuallyReadFolder(item);
}

bool AbstractFileManagerPlugin::reload( ProjectFolderItem* item )
{
    qCDebug(FILEMANAGER) << "reloading item" << item->path();
    auto job = d->eventuallyReadFolder( item->folder() );
    job->start();
    return true;
}

ProjectFolderItem* AbstractFileManagerPlugin::addFolder( const Path& folder,
        ProjectFolderItem * parent )
{
    qCDebug(FILEMANAGER) << "adding folder" << folder << "to" << parent->path();
    ProjectFolderItem* created = nullptr;
    d->stopWatcher(parent);
    if ( createFolder(folder.toUrl()) ) {
        created = createFolderItem( parent->project(), folder, parent );
        if (created) {
            emit folderAdded(created);
        }
    }
    d->continueWatcher(parent);
    return created;
}


ProjectFileItem* AbstractFileManagerPlugin::addFile( const Path& file,
        ProjectFolderItem * parent )
{
    qCDebug(FILEMANAGER) << "adding file" << file << "to" << parent->path();
    ProjectFileItem* created = nullptr;
    d->stopWatcher(parent);
    if ( createFile(file.toUrl()) ) {
        created = createFileItem( parent->project(), file, parent );
        if (created) {
            emit fileAdded(created);
        }
    }
    d->continueWatcher(parent);
    return created;
}

bool AbstractFileManagerPlugin::renameFolder(ProjectFolderItem* folder, const Path& newPath)
{
    qCDebug(FILEMANAGER) << "trying to rename a folder:" << folder->path() << newPath;
    return d->rename(folder, newPath);
}

bool AbstractFileManagerPlugin::renameFile(ProjectFileItem* file, const Path& newPath)
{
    qCDebug(FILEMANAGER) << "trying to rename a file:" << file->path() << newPath;
    return d->rename(file, newPath);
}

bool AbstractFileManagerPlugin::removeFilesAndFolders(const QList<ProjectBaseItem*> &items)
{
    bool success = true;
    foreach(ProjectBaseItem* item, items)
    {
        Q_ASSERT(item->folder() || item->file());

        ProjectFolderItem* parent = getParentFolder(item);
        d->stopWatcher(parent);

        success &= removeUrl(parent->project(), item->path().toUrl(), true);
        if ( success ) {
            if (item->file()) {
                emit fileRemoved(item->file());
            } else {
                Q_ASSERT(item->folder());
                emit folderRemoved(item->folder());
            }
            item->parent()->removeRow( item->row() );
        }

        d->continueWatcher(parent);
        if ( !success )
            break;
    }
    return success;
}

bool AbstractFileManagerPlugin::moveFilesAndFolders(const QList< ProjectBaseItem* >& items, ProjectFolderItem* newParent)
{
    bool success = true;
    foreach(ProjectBaseItem* item, items)
    {
        Q_ASSERT(item->folder() || item->file());

        ProjectFolderItem* oldParent = getParentFolder(item);
        d->stopWatcher(oldParent);
        d->stopWatcher(newParent);

        const Path oldPath = item->path();
        const Path newPath(newParent->path(), item->baseName());

        success &= renameUrl(oldParent->project(), oldPath.toUrl(), newPath. toUrl());
        if ( success ) {
            if (item->file()) {
                emit fileRemoved(item->file());
            } else {
                emit folderRemoved(item->folder());
            }
            oldParent->removeRow( item->row() );
            KIO::Job *readJob = d->eventuallyReadFolder(newParent);
            // reload first level synchronously, deeper levels will run async
            // this is required for code that expects the new item to exist after
            // this method finished
            readJob->exec();
        }

        d->continueWatcher(oldParent);
        d->continueWatcher(newParent);
        if ( !success )
            break;
    }
    return success;
}

bool AbstractFileManagerPlugin::copyFilesAndFolders(const Path::List& items, ProjectFolderItem* newParent)
{
    bool success = true;
    foreach(const Path& item, items)
    {
        d->stopWatcher(newParent);

        success &= copyUrl(newParent->project(), item.toUrl(), newParent->path().toUrl());
        if ( success ) {
            KIO::Job *readJob = d->eventuallyReadFolder(newParent);
            // reload first level synchronously, deeper levels will run async
            // this is required for code that expects the new item to exist after
            // this method finished
            readJob->exec();
        }

        d->continueWatcher(newParent);
        if ( !success )
            break;
    }
    return success;
}

bool AbstractFileManagerPlugin::isValid( const Path& path, const bool isFolder,
                                         IProject* project ) const
{
    return d->m_filters.isValid( path, isFolder, project );
}

ProjectFileItem* AbstractFileManagerPlugin::createFileItem( IProject* project, const Path& path,
                                                            ProjectBaseItem* parent )
{
    return new ProjectFileItem( project, path, parent );
}

ProjectFolderItem* AbstractFileManagerPlugin::createFolderItem( IProject* project, const Path& path,
                                                                ProjectBaseItem* parent )
{
    return new ProjectFolderItem( project, path, parent );
}

KDirWatch* AbstractFileManagerPlugin::projectWatcher( IProject* project ) const
{
    return d->m_watchers.value( project, nullptr );
}

//END Plugin

#include "moc_abstractfilemanagerplugin.cpp"
