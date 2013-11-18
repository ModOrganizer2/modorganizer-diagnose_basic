/*
Copyright (C) 2013 Sebastian Herbord. All rights reserved.

This file is part of the basic diagnosis plugin for Mod Organizer

This plugin is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This plugin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this plugin.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "diagnosebasic.h"
#include <report.h>
#include <utility.h>
#include <QtPlugin>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QMessageBox>
#include <QDateTime>
#include <regex>
#include <boost/assign.hpp>
#include <functional>


using namespace MOBase;


DiagnoseBasic::DiagnoseBasic()
{
}

bool DiagnoseBasic::init(IOrganizer *moInfo)
{
  m_MOInfo = moInfo;

  m_MOInfo->modList()->onModStateChanged([&] (const QString &modName, IModList::ModStates) {
                                            if (modName == "Overwrite") invalidate(); });
  m_MOInfo->pluginList()->onRefreshed([&] () { this->invalidate(); });

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
  return VersionInfo(1, 1, 0, VersionInfo::RELEASE_FINAL);
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

bool operator<(const DiagnoseBasic::Move &lhs, const DiagnoseBasic::Move &rhs) {
  if (lhs.item.modName != rhs.item.modName) return lhs.item.modName < rhs.item.modName;
  else return lhs.reference.modName < rhs.reference.modName;
}


bool DiagnoseBasic::assetOrder() const
{
  struct Sorter {
    struct {
      int operator()(const ListElement &lhs, const ListElement &rhs) {
        return lhs.modPriority < rhs.modPriority;
      }
    } minMod;

    std::vector <Move> moves;
    void operator()(const std::vector<ListElement> &list) {
      if (list.size() == 0) {
        return;
      }

      // generate a copy of list that contains each mod only once, otherwise
      // strange things happen if a mod contains multiple esps that are mixed
      // with esps from other mods
      std::vector<ListElement> modList;
      {
        std::set<QString> includedMods;
        foreach(const ListElement &ele, list) {
          if (includedMods.find(ele.modName) == includedMods.end()) {
            modList.push_back(ele);
            includedMods.insert(ele.modName);
          }
        }
      }

      std::vector<ListElement> sorted;
      {
        auto maxSeqBegin = modList.end();
        auto maxSeqEnd = modList.end();

        // first, determine the longest sequence of correctly sorted mods
        auto curSeqBegin = modList.begin();
        auto curSeqEnd = modList.begin();

        auto iter = modList.begin() + 1;
        for (; iter != modList.end(); ++iter) {
          if (iter->modPriority < curSeqEnd->modPriority) {
            // sequence ends
            if ((maxSeqBegin == modList.end()) || ((curSeqEnd - curSeqBegin) > (maxSeqEnd - maxSeqBegin))) {
              maxSeqBegin = curSeqBegin;
              maxSeqEnd = iter;
            }
            curSeqBegin = curSeqEnd = iter;
          } else {
            curSeqEnd = iter;
          }
        }

        if ((maxSeqBegin == modList.end()) || ((curSeqEnd - curSeqBegin) > (maxSeqEnd - maxSeqBegin))) {
          maxSeqBegin = curSeqBegin;
          maxSeqEnd = modList.end();
        }
        sorted = std::vector<ListElement>(maxSeqBegin, maxSeqEnd);
        modList.erase(maxSeqBegin, maxSeqEnd);
      }

      // now move the elements NOT in this sequence to the correct location within
      while (modList.begin() != modList.end()) {
        auto iter = modList.begin();
        bool found = false;
        auto targetIter = sorted.begin();
        for (; targetIter != sorted.end(); ++targetIter) {
          if (targetIter->pluginPriority > iter->pluginPriority) {
            moves.push_back(Move(*iter, *targetIter, Move::BEFORE));
            found = true;
            break;
          }
        }
        if (!found) {
          // add to end!
          moves.push_back(Move(*iter, *sorted.rbegin(), Move::AFTER));
        }
        sorted.insert(targetIter, *iter);
        modList.erase(iter);
      }
    }
  } minSorter;

  std::vector<ListElement> list;

  // list of mods containing conflicted scripts. We care only for those
  std::set<QString> scriptMods;
  foreach (const IOrganizer::FileInfo &pex, m_MOInfo->findFileInfos("scripts",
            [] (const IOrganizer::FileInfo &file) -> bool { return file.filePath.endsWith(".pex", Qt::CaseInsensitive); })) {
    if (pex.origins.size() > 1) {
      foreach (const QString &origin, pex.origins) {
        scriptMods.insert(origin);
      }
    }
  }

  // produce a list with the information we need: plugin, mod and the priority for each
  QStringList esps = m_MOInfo->findFiles("", [] (const QString &fileName) -> bool { return fileName.endsWith(".esp", Qt::CaseInsensitive); });
  foreach (const QString &esp, esps) {
    ListElement ele;
    ele.espName = QFileInfo(esp).fileName();
    ele.modName = m_MOInfo->pluginList()->origin(ele.espName);
    ele.pluginPriority = m_MOInfo->pluginList()->priority(ele.espName);
    ele.modPriority = m_MOInfo->modList()->priority(ele.modName);
    IModList::ModStates state = m_MOInfo->modList()->state(ele.modName);
    if (state.testFlag(IModList::STATE_EXISTS) && !state.testFlag(IModList::STATE_ESSENTIAL) &&
        (scriptMods.find(ele.modName) != scriptMods.end())) {
      list.push_back(ele);
    }
  }

  // sort the list by plugin priority
  std::sort(list.begin(), list.end(), [] (const ListElement &lhs, const ListElement &rhs) -> bool { return lhs.pluginPriority < rhs.pluginPriority; });

  // now determine the moves necessary to bring the mod list into this order
  minSorter(list);
  m_SuggestedMoves = minSorter.moves;

  return m_SuggestedMoves.size() > 0;
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
  if (nitpickInstalled()) {
    result.push_back(PROBLEM_NITPICKINSTALLED);
  }
  if (assetOrder()) {
    result.push_back(PROBLEM_ASSETORDER);
  }
  QStringList backups = QDir(m_MOInfo->profilePath()).entryList(QStringList() << "modlist.txt_backup_*");
  if (backups.size() > 0) {
    m_NewestModlistBackup = backups.last();
    result.push_back(PROBLEM_MODLISTBACKUP);
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
    case PROBLEM_ASSETORDER:
      return tr("Potential Mod order problem");
    case PROBLEM_MODLISTBACKUP:
      return tr("Modlist backup exists");
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
    case PROBLEM_NITPICKINSTALLED:
      return tr("You have the nitpick skse plugin installed. This plugin is not needed with Mod Organizer because MO already offers the same functionality. "
                "Worse: The two solutions may conflict so it's strongly suggested you remove this plugin.");
    case PROBLEM_ASSETORDER: {
      QString res = tr("The conflict resolution order for some mods with scripts differs from the corresponding esp. This can lead to subtle, hard to locate "
                       "bugs. You should re-order the affected mods. The following changes should fix the issue (They are applied automatically if you click \"Fix\"):<ul>");
      foreach (const Move &op, m_SuggestedMoves) {
        if (op.type == Move::BEFORE) {
          res += "<li>" + tr("Move %1 before %2 (%3/%4 - %5/%6)").arg(op.item.modName).arg(op.reference.modName).arg(op.item.pluginPriority).arg(op.reference.pluginPriority).arg(op.item.modPriority).arg(op.reference.modPriority) + "</li>";
        } else {
          res += "<li>" + tr("Move %1 after %2 (%3/%4 - %5/%6)").arg(op.item.modName).arg(op.reference.modName).arg(op.item.pluginPriority).arg(op.reference.pluginPriority).arg(op.item.modPriority).arg(op.reference.modPriority) + "</li>";
        }
      }
      res += "</ul>";
      return res;
    } break;
    case PROBLEM_MODLISTBACKUP: {
      uint timestamp = m_NewestModlistBackup.right(10).toULong();
      QDateTime time;
      time.setTime_t(timestamp);
      return tr("A previous operation created a backup of your mod list on %1.<br>"
                "This backup contains both the info which mods are enabled and the ordering.<br>"
                "You can restore that backup here.").arg(time.toString());
    } break;
    default:
      throw MyException(tr("invalid problem key %1").arg(key));
  }
}

bool DiagnoseBasic::hasGuidedFix(unsigned int key) const
{
  return (key == PROBLEM_ASSETORDER) || (key == PROBLEM_MODLISTBACKUP);
}

void DiagnoseBasic::startGuidedFix(unsigned int key) const
{
  switch (key) {
    case PROBLEM_ASSETORDER: {
      if (QMessageBox::warning(NULL, tr("Continue?"), tr("This <b>BETA</b> feature will rearrange your mods to eliminate all "
              "possible ordering conflicts. A backup of your mod list will be created. Proceed?"), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        shellCopy(QStringList(m_MOInfo->profilePath() + "/modlist.txt"),
                  QStringList(m_MOInfo->profilePath() + "/modlist.txt_backup_" + QString("%1").arg(QDateTime::currentDateTime().toTime_t())));
        foreach (const Move &op, m_SuggestedMoves) {
          int oldPriority = m_MOInfo->modList()->priority(op.item.modName);
          int targetPriority = -1;
          if (op.type == Move::BEFORE) {
            targetPriority = m_MOInfo->modList()->priority(op.reference.modName);
          } else {
            targetPriority = m_MOInfo->modList()->priority(op.reference.modName) + 1;
          }
          if (oldPriority < targetPriority) {
            --targetPriority;
          }
          m_MOInfo->modList()->setPriority(op.item.modName, targetPriority);
        }
      }
    } break;
    case PROBLEM_MODLISTBACKUP: {
      QMessageBox question(QMessageBox::Question, tr("Restore backup?"),
              tr("Do you want to restore this backup or delete it?"),
              QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
      question.setButtonText(QMessageBox::Yes, tr("Restore"));
      question.setButtonText(QMessageBox::No, tr("Delete"));

      question.exec();
      if (question.result() == QMessageBox::Yes) {
        shellMove(QStringList(m_MOInfo->profilePath() + "/" + m_NewestModlistBackup), QStringList(m_MOInfo->profilePath() + "/modlist.txt"));
        m_MOInfo->refreshModList(false);
      } else if (question.result() == QMessageBox::No) {
        shellDelete(QStringList(m_MOInfo->profilePath() + "/" + m_NewestModlistBackup));
      }
    } break;
    default: throw MyException(tr("invalid problem key %1").arg(key));
  }
}

#if QT_VERSION < QT_VERSION_CHECK(5,0,0)
Q_EXPORT_PLUGIN2(diagnosebasic, DiagnoseBasic)
#endif
