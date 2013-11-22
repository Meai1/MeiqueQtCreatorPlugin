#include "MeiqueProject.h"
#include "Constants.h"
#include "MeiqueDocument.h"
#include "MeiqueProjectNode.h"
#include "MeiqueManager.h"

#include <coreplugin/icontext.h>
#include <cpptools/cppmodelmanagerinterface.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <QMap>
#include <QProcess>
#include <QDebug>

#include <projectexplorer/headerpath.h>
#include <projectexplorer/kitinformation.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/toolchain.h>
#include <projectexplorer/target.h>


MeiqueProject::MeiqueProject(MeiqueManager* manager, const QString& fileName)
    : m_manager(manager)
    , m_fileName(fileName)
    , m_rootNode(new MeiqueProjectNode(fileName))
    , m_document(new MeiqueDocument(fileName))
{
    setProjectContext(Core::Context(MEIQUE_PROJECT_CONTEXT));
    setProjectLanguages(Core::Context(ProjectExplorer::Constants::LANG_CXX));

    QDir buildDir = QFileInfo(fileName).dir();
    // TODO make build dir configurable.
    if (!buildDir.cd("build")) {
        buildDir.mkdir("build");
        // TODO alert when this fail.
        buildDir.cd("build");
    }
    m_buildDir = buildDir.canonicalPath();

    parseProject();
}

MeiqueProject::~MeiqueProject()
{
    delete m_rootNode;
}

QString MeiqueProject::displayName() const
{
    return m_projectName;
}

Core::Id MeiqueProject::id() const
{
    return "MeiqueProject";
}

Core::IDocument* MeiqueProject::document() const
{
    return m_document;
}

ProjectExplorer::IProjectManager* MeiqueProject::projectManager() const
{
    return m_manager;
}

ProjectExplorer::ProjectNode* MeiqueProject::rootProjectNode() const
{
    return m_rootNode;
}

QStringList MeiqueProject::files(FilesMode fileMode) const
{
    return m_fileList;
}

void MeiqueProject::parseProject()
{
    m_fileList.clear();
    QStringList includeDirs;

    QProcess proc;
    proc.setWorkingDirectory(m_buildDir);
    // TODO: replace by meique found by the configuration
    proc.start("meique", QStringList() << "--meique-dump-project");
    proc.waitForStarted();
    proc.closeWriteChannel();
    proc.waitForFinished();
    QList<QByteArray> output = proc.readAllStandardOutput().split('\n');
    typedef QMap<ProjectExplorer::FolderNode*, QList<ProjectExplorer::FileNode*> > NodeTree;
    typedef QMapIterator<ProjectExplorer::FolderNode*, QList<ProjectExplorer::FileNode*> > NodeTreeIterator;

    NodeTree tree;
    ProjectExplorer::FolderNode* currentParentNode = m_rootNode;

    QList<CppTools::ProjectFile> projectFiles;
    foreach (QByteArray file, output) {
        if (file.startsWith("File:")) {
            tree[currentParentNode] << new ProjectExplorer::FileNode(file.mid(sizeof("File:")), ProjectExplorer::SourceType, false);
            QString filePath = file.mid(sizeof("File:"));
            m_fileList.append(filePath);
            projectFiles.append(CppTools::ProjectFile(filePath, CppTools::ProjectFile::Unclassified));
        } else if (file.startsWith("Target: ")) {
            currentParentNode = new ProjectExplorer::FolderNode(file.mid(sizeof("Target:")));
            tree[currentParentNode];
        } else if (file.startsWith("Include: ")) {
             includeDirs << file.mid(sizeof("Include:"));
        } else if (file.startsWith("Project: ")) {
            m_projectName = file.mid(sizeof("Project:"));
            emit displayNameChanged();
        }
    }

    m_rootNode->addFolderNodes(tree.keys(), m_rootNode);
    NodeTreeIterator i(tree);
    while (i.hasNext()) {
        i.next();
        m_rootNode->addFileNodes(i.value(), i.key());
    }

    emit fileListChanged();

    CppTools::CppModelManagerInterface* modelManager = CppTools::CppModelManagerInterface::instance();
    CppTools::CppModelManagerInterface::ProjectInfo pinfo = modelManager->projectInfo(this);

    pinfo.clearProjectParts();
    CppTools::ProjectPart::Ptr part(new CppTools::ProjectPart);
    part->includePaths = includeDirs;
    part->files = projectFiles;

    ProjectExplorer::Kit *k = activeTarget() ? activeTarget()->kit() : ProjectExplorer::KitManager::instance()->defaultKit();
    if (ProjectExplorer::ToolChain *tc = ProjectExplorer::ToolChainKitInformation::toolChain(k)) {
        part->cxxVersion = CppTools::ProjectPart::CXX11;
        part->cVersion = CppTools::ProjectPart::C11;
        part->cxxExtensions = CppTools::ProjectPart::GnuExtensions;
        QStringList cxxflags("-std=c++0x");
        part->defines = tc->predefinedMacros(cxxflags);

        foreach (const ProjectExplorer::HeaderPath &headerPath, tc->systemHeaderPaths(cxxflags, ProjectExplorer::SysRootKitInformation::sysRoot(k))) {
            if (headerPath.kind() == ProjectExplorer::HeaderPath::FrameworkHeaderPath)
                part->frameworkPaths.append(headerPath.path());
            else
                part->includePaths.append(headerPath.path());
        }
    }

    pinfo.appendProjectPart(part);
    modelManager->updateProjectInfo(pinfo);
    modelManager->updateSourceFiles(m_fileList);
}

#include "MeiqueProject.moc"
