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
#include <report.h>
#include <utility.h>
#include <QtPlugin>
#include <QFile>
#include <QDir>
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
  m_MOInfo->pluginList()->onRefreshed([&] () { this->invalidate(); });

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
      << PluginSetting("check_modorder", tr("Warn when MO determins the mod order may cause problems"), true)
      << PluginSetting("check_missingmasters", tr("Warn when there are esps with missing masters"), true);
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


bool DiagnoseBasic::overwriteFiles() const
{
  QDir dir(qApp->property("dataPath").toString() + "/overwrite");

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

bool operator<(const DiagnoseBasic::Move &lhs, const DiagnoseBasic::Move &rhs)
{
  if (lhs.item.modName != rhs.item.modName) return lhs.item.modName < rhs.item.modName;
  else return lhs.reference.modName < rhs.reference.modName;
}


void DiagnoseBasic::topoSort(std::vector<DiagnoseBasic::ListElement> &list) const
{
  typedef std::pair<int, int> Edge;
  std::vector<Edge> before;

  // create a graph with edges where each edge tells us that mod a has to come before mod b
  // this takes into account only pairs of mods that actually have conflicting scripts
  for (unsigned i = 0; i < list.size(); ++i) {
    for (unsigned j = i + 1; j < list.size(); ++j) {
      if (!(list[i].relevantScripts & list[j].relevantScripts).empty()) {
        before.push_back(Edge(i, j));
      }
    }
  }

  {
    typedef boost::adjacency_list<boost::vecS,boost::vecS, boost::bidirectionalS, boost::property<boost::vertex_color_t, boost::default_color_type>> Graph;
    using namespace boost;
    Graph graph(before.begin(), before.end(), list.size());
    typedef graph_traits<Graph>::vertex_descriptor Vertex;
    typedef std::list<Vertex> Order;

    // figure out disconnected components of the graph
    std::vector<int> component(num_vertices(graph));
    if (component.size() == 0) {
      throw MyException(tr("failed to sort"));
    }
    connected_components(graph, &component[0]);
    for (int i = 0; i != component.size(); ++i) {
      list[i].sortGroup = component[i];
    }

    Order order;
    // do the actual sorting. This sorts the graph in full although the order between unconnected components doesn't
    // really matter to us
    boost::topological_sort(graph, std::front_inserter(order));
  }
}


void DiagnoseBasic::Sorter::sortGroup(std::vector<ListElement> modList)
{
  std::vector<ListElement> sorted;
  {
    auto maxSeqBegin = modList.end();
    auto maxSeqEnd = modList.end();
    int maxSeqAvoidMove = 0;

    // first, determine the longest sequence of correctly sorted mods
    auto curSeqBegin = modList.begin();
    auto curSeqEnd = modList.begin();

    int curSeqAvoidMove = 0;

    auto iter = modList.begin() + 1;
    for (; iter != modList.end(); ++iter) {
      if ((iter->pluginPriority != curSeqEnd->pluginPriority)
          && (iter->modPriority < curSeqEnd->modPriority)) {
        // sequence ends
        // use this sequence of correctly sorted mods if it is longer than the previously longest
        // sequence and doesn't have fewer mods that don't want to move. Thus the need for mods to
        // stay in place beats our gole to have the minimal number of moves
        if ((maxSeqBegin == modList.end())
            || (((curSeqEnd - curSeqBegin) > (maxSeqEnd - maxSeqBegin))
                && (curSeqAvoidMove >= maxSeqAvoidMove))) {
          maxSeqBegin = curSeqBegin;
          maxSeqEnd = iter;
          maxSeqAvoidMove = curSeqAvoidMove;
        }
        curSeqBegin = curSeqEnd = iter;
        curSeqAvoidMove = 0;
      } else {
        curSeqEnd = iter;
        if (iter->avoidMove) {
          ++curSeqAvoidMove;
        }
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
    if (!found
        && (iter->pluginPriority != (*sorted.rbegin()).pluginPriority)) {
      // add to end!
      moves.push_back(Move(*iter, *sorted.rbegin(), Move::AFTER));
    }
    sorted.insert(targetIter, *iter);
    modList.erase(iter);
  }
}

void DiagnoseBasic::Sorter::operator()(std::vector<ListElement> modList)
{
  if (modList.size() == 0) {
    return;
  }

  int currentGroup = 0;
  while (true) {
    std::vector<ListElement> filtered;
    std::copy_if(modList.begin(), modList.end(), std::back_inserter(filtered),
                 [currentGroup] (const ListElement &ele) -> bool { return ele.sortGroup == currentGroup; });
    ++currentGroup;
    if (filtered.size() == 0) {
      break;
    } else if (filtered.size() == 1) {
      // skip if there is only one element, there can't be a necessary move
      continue;
    }
    sortGroup(filtered);
  }
}

bool DiagnoseBasic::assetOrder() const
{
  Sorter minSorter;

  std::vector<ListElement> modList;

  // list of mods containing conflicted scripts. We care only for those
  std::map<QString, QSet<QString>> scriptMods;
  foreach (const IOrganizer::FileInfo & pex, m_MOInfo->findFileInfos("scripts",
        [] (const IOrganizer::FileInfo &file) -> bool { return file.filePath.endsWith(".pex", Qt::CaseInsensitive); })) {
    QStringList origins = pex.origins;
    origins.removeAll("data"); // ignore files in base directory
    if (origins.size() > 1) {
      foreach(const QString &origin, origins) {
        scriptMods[origin].insert(pex.filePath);
      }
    }
  }

  // produce a list with the information we need: plugin, mod and the priority for each
  QStringList esps = m_MOInfo->findFiles("",
      [] (const QString &fileName) -> bool { return fileName.endsWith(".esp", Qt::CaseInsensitive)
                                                  || fileName.endsWith(".esm", Qt::CaseInsensitive); });
  foreach (const QString &esp, esps) {
    foreach (const QString origin, m_MOInfo->getFileOrigins(esp)) {
      ListElement ele;

      ele.espName = QFileInfo(esp).fileName();
      ele.modName = origin;
      ele.pluginPriority = m_MOInfo->pluginList()->priority(ele.espName);
      ele.modPriority = m_MOInfo->modList()->priority(ele.modName);
      IModList::ModStates state = m_MOInfo->modList()->state(ele.modName);
      ele.avoidMove = state.testFlag(IModList::STATE_ESSENTIAL);
      auto iter = scriptMods.find(ele.modName);
      if (state.testFlag(IModList::STATE_EXISTS)
          && (iter != scriptMods.end())) {
        ele.relevantScripts = iter->second;
        modList.push_back(ele);
      }
    }
  }

  // generate a copy of list that contains each mod only once, otherwise
  // strange things happen if a mod contains multiple esps that are mixed
  // with esps from other mods
  std::vector<ListElement> distinctModList;
  {
    // sort the input list so we get predictable results
    std::sort(modList.begin(), modList.end(),
              [] (const ListElement &lhs, const ListElement &rhs) -> bool { return lhs.pluginPriority < rhs.pluginPriority; });

    std::set<QString> includedMods;
    foreach(const ListElement &ele, modList) {
      if (includedMods.find(ele.modName) == includedMods.end()) {
        distinctModList.push_back(ele);
        includedMods.insert(ele.modName);
      }
    }
  }

  if (distinctModList.size() == 0) {
    return true;
  }

  // sort the list by plugin priority. This step is probably unnecessary as the list was already sorted when we removed duplicates
  std::sort(distinctModList.begin(), distinctModList.end(),
            [] (const ListElement &lhs, const ListElement &rhs) -> bool { return lhs.pluginPriority < rhs.pluginPriority; });

  if (distinctModList.size() > 0) {
    topoSort(distinctModList);

    // now determine the moves necessary to bring the mod list into this order
    minSorter(distinctModList);
    m_SuggestedMoves = minSorter.moves;
    return m_SuggestedMoves.size() > 0;
  } else {
    return false;
  }
}

bool DiagnoseBasic::missingMasters() const
{
  std::set<QString> enabledPlugins;

  QStringList esps = m_MOInfo->findFiles("",
      [] (const QString &fileName) -> bool { return fileName.endsWith(".esp", Qt::CaseInsensitive)
                                                  || fileName.endsWith(".esm", Qt::CaseInsensitive); });
  // gather enabled masters first
  foreach (const QString &esp, esps) {
    QString baseName = QFileInfo(esp).fileName();
    if (m_MOInfo->pluginList()->state(baseName) == IPluginList::STATE_ACTIVE) {
      enabledPlugins.insert(baseName);
    }
  }

  m_MissingMasters.clear();
  // for each required master in each esp, test if it's in the list of enabled masters.
  foreach (const QString &esp, esps) {
    QString baseName = QFileInfo(esp).fileName();
    if (m_MOInfo->pluginList()->state(baseName) == IPluginList::STATE_ACTIVE) {
      foreach (const QString master, m_MOInfo->pluginList()->masters(baseName)) {
        if (enabledPlugins.find(master) == enabledPlugins.end()) {
          m_MissingMasters.insert(master);
        }
      }
    }
  }
  return m_MissingMasters.size() > 0;
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
      foreach(const QString &def, defaultFonts) {
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
  if (m_MOInfo->pluginSetting(name(), "check_modorder").toBool() && assetOrder()) {
    result.push_back(PROBLEM_ASSETORDER);
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
    case PROBLEM_ASSETORDER:
      return tr("Potential Mod order problem");
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
    case PROBLEM_ASSETORDER: {
      QString res = tr("The conflict resolution order for some mods containing scripts differs from that of the corresponding esp.<br>"
                       "This may lead to subtle, hard to locate bugs. You should re-order the affected mods (left list!).<br>"
                       "There is no way to reliably know if each of these changes is absolutely necessary but its definitively safer.<br>"
                       "If someone suggested you ignore this message, please give them a proper slapping from me. <b>Do not ignore this warning</b><br>"
                       "The following changes should prevent these kinds of errors:") + "<ul>";
      foreach(const Move &op, m_SuggestedMoves) {
        QString itemName = m_MOInfo->modList()->displayName(op.item.modName);
        QString referenceName = m_MOInfo->modList()->displayName(op.reference.modName);
        if (op.type == Move::BEFORE) {
          res += "<li>" + tr("Move %1 before %2").arg(itemName).arg(referenceName) + "</li>";
        } else {
          res += "<li>" + tr("Move %1 after %2").arg(itemName).arg(referenceName) + "</li>";
        }
      }
      res += "</ul>";
      return res;
    } break;
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
  return (key == PROBLEM_ASSETORDER) || (key == PROBLEM_PROFILETWEAKS);
}

void DiagnoseBasic::startGuidedFix(unsigned int key) const
{
  switch (key) {
    case PROBLEM_ASSETORDER: {
      if (QMessageBox::warning(NULL, tr("Continue?"), tr("This <b>BETA</b> feature will rearrange your mods to eliminate all "
              "possible ordering conflicts. Proceed?"), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
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
    case PROBLEM_PROFILETWEAKS: {
      shellDeleteQuiet(m_MOInfo->profilePath() + "/profile_tweaks.ini");
    } break;
    default:
      throw MyException(tr("invalid problem key %1").arg(key));
  }
}

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
Q_EXPORT_PLUGIN2(diagnosebasic, DiagnoseBasic)
#endif
