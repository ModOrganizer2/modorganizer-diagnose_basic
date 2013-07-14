#ifndef DIAGNOSEBASIC_H
#define DIAGNOSEBASIC_H


#include <iplugin.h>
#include <iplugindiagnose.h>
#include <imoinfo.h>


class DiagnoseBasic : public QObject, MOBase::IPlugin, MOBase::IPluginDiagnose
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginDiagnose)
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
  Q_PLUGIN_METADATA(IID "org.tannin.DiagnoseBasic" FILE "diagnosebasic.json")
#endif

public:

  DiagnoseBasic();

public: // IPlugin

  virtual bool init(MOBase::IOrganizer *moInfo);
  virtual QString name() const;
  virtual QString author() const;
  virtual QString description() const;
  virtual MOBase::VersionInfo version() const;
  virtual bool isActive() const;
  virtual QList<MOBase::PluginSetting> settings() const;

public: // IPluginDiagnose

  virtual std::vector<unsigned int> activeProblems() const;
  virtual QString shortDescription(unsigned int key) const;
  virtual QString fullDescription(unsigned int key) const;
  virtual bool hasGuidedFix(unsigned int key) const;
  virtual void startGuidedFix(unsigned int key) const;

private:

  bool errorReported() const;
  bool overwriteFiles() const;
  bool invalidFontConfig() const;

private:

  static const unsigned int PROBLEM_ERRORLOG = 1;
  static const unsigned int PROBLEM_OVERWRITE = 2;
  static const unsigned int PROBLEM_INVALIDFONT = 3;

  static const unsigned int NUM_CONTEXT_ROWS = 5;

private:

  const MOBase::IOrganizer *m_MOInfo;
  mutable QString m_ErrorMessage;

};

#endif // DIAGNOSEBASIC_H
