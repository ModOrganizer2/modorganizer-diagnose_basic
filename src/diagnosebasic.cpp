#include "diagnosebasic.h"
#include <report.h>
#include <utility.h>
#include <QtPlugin>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <regex>
#include <boost/assign.hpp>


using namespace MOBase;


DiagnoseBasic::DiagnoseBasic()
{
}

bool DiagnoseBasic::init(IOrganizer *moInfo)
{
  m_MOInfo = moInfo;
  return true;
}

QString DiagnoseBasic::name() const
{
  return tr("Basic diagnosis plugin");
}

QString DiagnoseBasic::author() const
{
  return "Tannin";
}

QString DiagnoseBasic::description() const
{
  return tr("Checks for problems unrelated to other plugins");
}

VersionInfo DiagnoseBasic::version() const
{
  return VersionInfo(1, 0, 0, VersionInfo::RELEASE_FINAL);
}

bool DiagnoseBasic::isActive() const
{
  return true;
}

QList<PluginSetting> DiagnoseBasic::settings() const
{
  return QList<PluginSetting>();
}


bool DiagnoseBasic::errorReported() const
{
  QDir dir(QCoreApplication::applicationDirPath() + "/logs");
  QFileInfoList files = dir.entryInfoList(QStringList("ModOrganizer_??_??_??_??_??.log"),
                                          QDir::Files, QDir::Name | QDir::Reversed);

  if (files.count() > 0) {
    QString logFile = files.at(0).absoluteFilePath();
    QFile file(logFile);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      char buffer[1024];
      int line = 0;
      qint64 lineLengths[NUM_CONTEXT_ROWS];
      for (int i = 0; i < NUM_CONTEXT_ROWS; ++i) {
        lineLengths[i] = 0;
      }
      while (!file.atEnd()) {
        lineLengths[line % NUM_CONTEXT_ROWS] = file.readLine(buffer, 1024) + 1;
        if (strncmp(buffer, "ERROR", 5) == 0) {
          qint64 sumChars = 0;
          for (int i = 0; i < NUM_CONTEXT_ROWS; ++i) {
            sumChars += lineLengths[i];
          }
          file.seek(file.pos() - sumChars);
          m_ErrorMessage = "";
          for (int i = 0; i < 2 * NUM_CONTEXT_ROWS; ++i) {
            file.readLine(buffer, 1024);
            QString lineString = QString::fromUtf8(buffer);
            if (lineString.startsWith("ERROR")) {
              m_ErrorMessage += "<b>" + lineString + "</b>";
            } else {
              m_ErrorMessage += lineString;
            }
          }
          return true;
        }

        // prevent this function from taking forever
        if (line++ >= 50000) {
          break;
        }
      }
    }
  }

  return false;
}


bool DiagnoseBasic::overwriteFiles() const
{
  QDir dir(QCoreApplication::applicationDirPath() + "/overwrite");
  return dir.count() != 2; // account for . and ..
}

bool DiagnoseBasic::invalidFontConfig() const
{
  if (m_MOInfo->gameInfo().type() != IGameInfo::TYPE_SKYRIM) {
    // this check is only for skyrim
    return false;
  }

  // files from skyrim_interface.bsa
  static std::vector<QString> defaultFonts = boost::assign::list_of("interface\\fonts_console.swf")
                                                                   ("interface\\fonts_en.swf");

  QString configPath = m_MOInfo->resolvePath("interface/fontconfig.txt");
  if (configPath.isEmpty()) {
    return false;
  }
  QFile config(configPath);
  if (!config.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qDebug("failed to open %s", qPrintable(configPath));
    return false;
  }

  std::tr1::regex exp("^fontlib \"([^\"]*)\"$");
  while (!config.atEnd()) {
    QByteArray row = config.readLine();
    std::tr1::cmatch match;
    if (std::tr1::regex_search(row.constData(), match, exp)) {
      std::string temp = match[1];
      QString path(temp.c_str());
      bool isDefault = false;
      foreach (const QString &def, defaultFonts) {
        if (QString::compare(def, path, Qt::CaseInsensitive) == 0) {
          isDefault = true;
          break;
        }
      }

      if (!isDefault && m_MOInfo->resolvePath(path).isEmpty()) {
        return true;
      }
    }
  }
  return false;
}


std::vector<unsigned int> DiagnoseBasic::activeProblems() const
{
  std::vector<unsigned int> result;

  if (errorReported()) {
    result.push_back(PROBLEM_ERRORLOG);
  }
  if (overwriteFiles()) {
    result.push_back(PROBLEM_OVERWRITE);
  }
  if (invalidFontConfig()) {
    result.push_back(PROBLEM_INVALIDFONT);
  }

  return result;
}

QString DiagnoseBasic::shortDescription(unsigned int key) const
{
  switch (key) {
    case PROBLEM_ERRORLOG:
      return tr("There was an error reported recently");
    case PROBLEM_OVERWRITE:
      return tr("There are files in your overwrite mod");
    case PROBLEM_INVALIDFONT:
      return tr("Your font configuration may be broken");
    default:
      throw MyException(tr("invalid problem key %1").arg(key));
  }
}

QString DiagnoseBasic::fullDescription(unsigned int key) const
{
  switch (key) {
    case PROBLEM_ERRORLOG:
      return "<code>" + m_ErrorMessage.replace("\n", "<br>") + "</code>";
    case PROBLEM_OVERWRITE:
      return tr("Files in the <font color=\"red\"><i>Overwrite</i></font> mod are are usually files created by an external tool (i.e. Wrye Bash, Automatic Variants, ...).<br>"
                "It is advisable you empty Overwrite directory by moving those files to an existing mod. You can do this by double-clicking the <font color=\"red\"><i>Overwrite</i></font> mod and use drag&drop to move the files to a mod.<br>"
                "Alternatively, right-click on <font color=\"red\"><i>Overwrite</i></font> and create a new regular mod from the files there.<br>"
                "<br>"
                "Why is this necessary? Generated files may depend on the other mods active in a profile and may thus be incompatible with a different profile (i.e. bashed patches from Wrye Bash). "
                "On the other hand the file may be necessary in all profiles (i.e. dlc esms after cleaning with TESVEdit)<br>"
                "This can NOT be automated you HAVE to read up on the tools you use and make an educated decision.");
    case PROBLEM_INVALIDFONT:
      return tr("Your current configuration seems to reference a font that is not installed. You may see only boxes instead of letters.<br>"
                "The font configuration is in Data\\interface\\fontconfig.txt. Most likely you have a broken installation of a font replacer mod.");
    default:
      throw MyException(tr("invalid problem key %1").arg(key));
  }
}

bool DiagnoseBasic::hasGuidedFix(unsigned int) const
{
  return false;
}

void DiagnoseBasic::startGuidedFix(unsigned int key) const
{
  throw MyException(tr("invalid problem key %1").arg(key));
}

#if QT_VERSION < QT_VERSION_CHECK(5,0,0)
Q_EXPORT_PLUGIN2(diagnosebasic, DiagnoseBasic)
#endif
