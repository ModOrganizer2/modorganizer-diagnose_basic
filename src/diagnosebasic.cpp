/*
 * Copyright (C) 2013 Sebastian Herbord. All rights reserved.
 *
 * This file is part of the basic diagnosis plugin for Mod Organizer
 *
 * This plugin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This plugin is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this plugin.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "diagnosebasic.h"

#include "filenamestring.h"
#include "iplugingame.h"
#include <report.h>
#include <utility.h>
#include <imodlist.h>
#include <ipluginlist.h>

#include <QtPlugin>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QDebug>
#include <QCoreApplication>
#include <QMessageBox>
#include <QDateTime>

#include <regex>
#include <functional>
#include <vector>
#include <algorithm>

#pragma warning( push, 2 )
#include <boost/assign.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/topological_sort.hpp>
#include <boost/graph/connected_components.hpp>
#pragma warning( pop )


using namespace MOBase;


DiagnoseBasic::DiagnoseBasic()
  : m_MOInfo(nullptr)
{
}

bool DiagnoseBasic::init(IOrganizer *moInfo)
{
  m_MOInfo = moInfo;

  m_MOInfo->modList()->onModStateChanged([&] (const QString &modName, IModList::ModStates) {
                                           if (modName == "Overwrite") invalidate();
                                         });
  m_MOInfo->modList()->onModMoved([&] (const QString&, int, int) {
                                         // invalidates only the assetOrder check but there is currently no way to recheck individual
                                         // checks
                                         invalidate();
                                      });
  m_MOInfo->pluginList()->onPluginMoved([&] (const QString&, int, int) {
                                         invalidate();
                                      });
  m_MOInfo->pluginList()->onRefreshed([&] () { invalidate(); });
  m_MOInfo->pluginList()->onPluginStateChanged([&] (const QString &, IPluginList::PluginStates) {
    invalidate();
  });

  return true;
}

QString DiagnoseBasic::name() const
{
  return "Basic diagnosis plugin";
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
  return VersionInfo(1, 1, 2, VersionInfo::RELEASE_FINAL);
}

bool DiagnoseBasic::isActive() const
{
  return true;
}

QList<PluginSetting> DiagnoseBasic::settings() const
{
  return QList<PluginSetting>()
      << PluginSetting("check_errorlog", tr("Warn when an error occured last time an application was run"), true)
      << PluginSetting("check_overwrite", tr("Warn when there are files in the overwrite directory"), true)
      << PluginSetting("check_font", tr("Warn when the font configuration refers to files that aren't installed"), true)
      << PluginSetting("check_conflict", tr("Warn when mods are installed that conflict with MO functionality"), true)
      << PluginSetting("check_missingmasters", tr("Warn when there are esps with missing masters"), true)
      << PluginSetting("ow_ignore_empty", tr("Ignore empty directories when checking overwrite directory"), false)
      << PluginSetting("ow_ignore_log", tr("Ignore .log files and empty directories when checking overwrite directory"), false)
     ;
}


bool DiagnoseBasic::errorReported() const
{
  QDir dir(qApp->property("dataPath").toString() + "/logs");
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

bool DiagnoseBasic::checkEmpty(QString const &path) const
{
  QDir dir(path);
  dir.setFilter(QDir::Files | QDir::Hidden | QDir::System);

  //Search files first
  for (QString const &f : dir.entryList()) {
    FileNameString file(f);
    if (! m_MOInfo->pluginSetting(name(), "ow_ignore_log").toBool() ||
        ! file.endsWith(".log")) {
      return false;
    }
  }

  //Then directories
  dir.setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
  for (QFileInfo const &subdir : dir.entryInfoList()) {
    if (!checkEmpty(subdir.absoluteFilePath())) {
      return false;
    }
  }

  return true;
}

bool DiagnoseBasic::overwriteFiles() const
{
  //QString dirname(qApp->property("dataPath").toString() + "/overwrite");
  QString dirname(m_MOInfo->overwritePath());
  if (m_MOInfo->pluginSetting(name(), "ow_ignore_empty").toBool() ||
      m_MOInfo->pluginSetting(name(), "ow_ignore_log").toBool()) {
    return !checkEmpty(dirname);
  }
  QDir dir(dirname);
  return dir.count() != 2; // account for . and ..
}

bool DiagnoseBasic::nitpickInstalled() const
{
  QString path = m_MOInfo->resolvePath("skse/plugins/nitpick.dll");

  return !path.isEmpty();
}


/// unused code to remove duplicates from a vector
template<typename T>
void makeUnique(std::vector<T> &vector)
{
  std::set<T> done;

  auto read = vector.begin();
  auto write = vector.begin();

  for (; read != vector.end(); ++read) {
    if (done.insert(*read).second) {
      *write = *read;
      ++write;
    }
  }

  vector.erase(write, vector.end());
}


bool DiagnoseBasic::missingMasters() const
{
  std::set<QString> enabledPlugins;

  QStringList esps = m_MOInfo->findFiles("",
      [] (const QString &fileName) -> bool { return fileName.endsWith(".esp", Qt::CaseInsensitive)
                                                  || fileName.endsWith(".esm", Qt::CaseInsensitive); });
  // gather enabled masters first
  for (const QString &esp : esps) {
    QString baseName = QFileInfo(esp).fileName();
    if (m_MOInfo->pluginList()->state(baseName) == IPluginList::STATE_ACTIVE) {
      enabledPlugins.insert(baseName.toLower());
    }
  }

  m_MissingMasters.clear();
  // for each required master in each esp, test if it's in the list of enabled masters.
  for (const QString &esp : esps) {
    QString baseName = QFileInfo(esp).fileName();
    if (m_MOInfo->pluginList()->state(baseName) == IPluginList::STATE_ACTIVE) {
      for (const QString master : m_MOInfo->pluginList()->masters(baseName)) {
        if (enabledPlugins.find(master.toLower()) == enabledPlugins.end()) {
          m_MissingMasters.insert(master);
        }
      }
    }
  }
  return !m_MissingMasters.empty();
}

bool DiagnoseBasic::invalidFontConfig() const
{
  if ((m_MOInfo->managedGame()->gameName() != "Skyrim") && (m_MOInfo->managedGame()->gameName() != "SkyrimSE"))  {
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
      for (const QString &def : defaultFonts) {
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

  if (m_MOInfo->pluginSetting(name(), "check_errorlog").toBool() && errorReported()) {
    result.push_back(PROBLEM_ERRORLOG);
  }
  if (m_MOInfo->pluginSetting(name(), "check_overwrite").toBool() && overwriteFiles()) {
    result.push_back(PROBLEM_OVERWRITE);
  }
  if (m_MOInfo->pluginSetting(name(), "check_font").toBool() && invalidFontConfig()) {
    result.push_back(PROBLEM_INVALIDFONT);
  }
  if (m_MOInfo->pluginSetting(name(), "check_conflict").toBool() && nitpickInstalled()) {
    result.push_back(PROBLEM_NITPICKINSTALLED);
  }
  if (m_MOInfo->pluginSetting(name(), "check_missingmasters").toBool() && missingMasters()) {
    result.push_back(PROBLEM_MISSINGMASTERS);
  }
  if (QFile::exists(m_MOInfo->profilePath() + "/profile_tweaks.ini")) {
    result.push_back(PROBLEM_PROFILETWEAKS);
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
    case PROBLEM_NITPICKINSTALLED:
      return tr("Nitpick installed");
    case PROBLEM_PROFILETWEAKS:
      return tr("Ini Tweaks overwritten");
    case PROBLEM_MISSINGMASTERS:
      return tr("Missing Masters");
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
                "It is advisable you empty the Overwrite directory by moving those files to an existing mod. You can do this by double-clicking the <font color=\"red\"><i>Overwrite</i></font> mod and use drag&drop to move the files to a mod.<br>"
                "Alternatively, right-click on <font color=\"red\"><i>Overwrite</i></font> and create a new regular mod from the files there.<br>"
                "<br>"
                "Why is this necessary? Generated files may depend on the other mods active in a profile and may thus be incompatible with a different profile (i.e. bashed patches from Wrye Bash). "
                "On the other hand the file may be necessary in all profiles (i.e. dlc esms after cleaning with TESVEdit)<br>"
                "This can NOT be automated you HAVE to read up on the tools you use and make an educated decision.");
    case PROBLEM_INVALIDFONT:
      return tr("Your current configuration seems to reference a font that is not installed. You may see only boxes instead of letters.<br>"
                "The font configuration is in Data\\interface\\fontconfig.txt. Most likely you have a broken installation of a font replacer mod.");
    case PROBLEM_NITPICKINSTALLED:
      return tr("You have the nitpick skse plugin installed. This plugin is not needed with Mod Organizer because MO already offers the same functionality. "
                "Worse: The two solutions may conflict so it's strongly suggested you remove this plugin.");
    case PROBLEM_PROFILETWEAKS: {
      QString fileContent = readFileText(m_MOInfo->profilePath() + "/profile_tweaks.ini");
      return tr("Settings provided in ini tweaks have been overwritten in-game or in an applications.<br>"
                "These overwrites are stored in a separate file (<i>profile_tweaks.ini</i> within the profile directory)<br>"
                "to keep ini-tweaks in their original state but you should really get rid of this file as there is<br>"
                "no tool support in MO to work on it. <br>"
                "Advice: Copy settings you want to keep to an appropriate ini tweak, then delete <i>profile_tweaks.ini</i>.<br>"
                "Hitting the <i>Fix</i> button will delete that file")
             + "<hr><i>profile_tweaks.ini:</i><pre>" + fileContent + "</pre>";
    } break;
    case PROBLEM_MISSINGMASTERS: {
      return tr("The masters for some plugins (esp/esm) are not enabled.<br>"
                "The game will crash unless you install and enable the following plugins: ")
             + "<ul><li>" + SetJoin(m_MissingMasters, "</li><li>") + "</li></ul>";
    } break;
    default:
      throw MyException(tr("invalid problem key %1").arg(key));
  }
}

bool DiagnoseBasic::hasGuidedFix(unsigned int key) const
{
  return (key == PROBLEM_PROFILETWEAKS);
}

void DiagnoseBasic::startGuidedFix(unsigned int key) const
{
  switch (key) {
    case PROBLEM_PROFILETWEAKS: {
      shellDeleteQuiet(m_MOInfo->profilePath() + "/profile_tweaks.ini");
    } break;
    default:
      throw MyException(tr("invalid problem key %1").arg(key));
  }
}
