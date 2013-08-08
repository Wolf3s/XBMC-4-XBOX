#pragma once
/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */
#include "Addon.h"
#include "include/xbmc_addon_dll.h"
#include "tinyXML/tinyxml.h"
#include "utils/CriticalSection.h"
#include "StdString.h"
#include "utils/Job.h"
#include "utils/Stopwatch.h"
#include <vector>
#include <map>
#include <deque>

class DllLibCPluff;
extern "C"
{
#include "lib/cpluff/libcpluff/cpluff_xbox.h"
}

namespace ADDON
{
  typedef std::vector<AddonPtr> VECADDONS;
  typedef std::vector<AddonPtr>::iterator IVECADDONS;
  typedef std::map<TYPE, VECADDONS> MAPADDONS;
  typedef std::map<TYPE, VECADDONS>::iterator IMAPADDONS;
  typedef std::deque<cp_cfg_element_t*> DEQUEELEMENTS;
  typedef std::deque<cp_cfg_element_t*>::iterator IDEQUEELEMENTS;

  const CStdString ADDON_METAFILE             = "description.xml";
  const CStdString ADDON_VIS_EXT              = "*.vis";
  const CStdString ADDON_PYTHON_EXT           = "*.py";
  const CStdString ADDON_SCRAPER_EXT          = "*.xml";
  const CStdString ADDON_SCREENSAVER_EXT      = "*.xbs";
  const CStdString ADDON_DSP_AUDIO_EXT        = "*.adsp";
  const CStdString ADDON_VERSION_RE = "(?<Major>\\d*)\\.?(?<Minor>\\d*)?\\.?(?<Build>\\d*)?\\.?(?<Revision>\\d*)?";

  /**
  * Class - IAddonCallback
  * This callback should be inherited by any class which manages
  * specific addon types. Could be mostly used for Dll addon types to handle
  * cleanup before restart/removal
  */
  class IAddonMgrCallback
  {
    public:
      virtual ~IAddonMgrCallback() {};
      virtual bool RequestRestart(AddonPtr addon, bool datachanged)=0;
      virtual bool RequestRemoval(AddonPtr addon)=0;
  };

  /**
  * Class - CAddonMgr
  * Holds references to all addons, enabled or
  * otherwise. Services the generic callbacks available
  * to all addon variants.
  */
  class CAddonMgr : public IJobCallback
  {
  public:
    static CAddonMgr &Get();
    bool Init();
    void DeInit();

    IAddonMgrCallback* GetCallbackForType(TYPE type);
    bool RegisterAddonMgrCallback(TYPE type, IAddonMgrCallback* cb);
    void UnregisterAddonMgrCallback(TYPE type);

    /* Addon access */
    bool GetDefault(const TYPE &type, AddonPtr &addon, const CONTENT_TYPE &content = CONTENT_NONE);
    bool GetAddon(const CStdString &str, AddonPtr &addon, const TYPE &type = ADDON_UNKNOWN, bool enabledOnly = true);
    AddonPtr GetAddon2(const CStdString &str);
    bool HasAddons(const TYPE &type, const CONTENT_TYPE &content = CONTENT_NONE, bool enabledOnly = true);
    bool GetAddons(const TYPE &type, VECADDONS &addons, const CONTENT_TYPE &content = CONTENT_NONE, bool enabled = true);
    bool GetAllAddons(VECADDONS &addons, bool enabledOnly = true);
    CStdString GetString(const CStdString &id, const int number);
    
    bool AddonFromFolder(const CStdString& strFolder, AddonPtr& addon);
    static bool GetTranslatedString(const TiXmlElement *xmldoc, const char *tag, CStdString& data);
    const char *GetTranslatedString(const cp_cfg_element_t *root, const char *tag);
    static AddonPtr AddonFromProps(AddonProps& props);
    void UpdateRepos();
    void FindAddons();

    /* libcpluff */
    CStdString GetExtValue(cp_cfg_element_t *base, const char *path);
    std::vector<CStdString> GetExtValues(cp_cfg_element_t *base, const char *path);
    const cp_extension_t *GetExtension(const cp_plugin_info_t *props, const char *extension);

    /*! \brief Load the addon in the given path
     This loads the addon using c-pluff which parses the addon descriptor file.
     \param path folder that contains the addon.
     \param addon [out] returned addon.
     \return true if addon is set, false otherwise.
     */
    bool LoadAddonDescription(const CStdString &path, AddonPtr &addon);

    /*! \brief Parse a repository XML file for addons and load their descriptors
     A repository XML is essentially a concatenated list of addon descriptors.
     \param root Root element of an XML document.
     \param addons [out] returned list of addons.
     \return true if the repository XML file is parsed, false otherwise.
     */
    bool AddonsFromRepoXML(const TiXmlElement *root, VECADDONS &addons);
    ADDONDEPS GetDeps(const CStdString& id);
  private:
    void LoadAddons(const CStdString &path, 
                    std::map<CStdString, AddonPtr>& unresolved);

    void OnJobComplete(unsigned int jobID, bool sucess, CJob* job);

    /* libcpluff */
    bool GetExtensions(const TYPE &type, VECADDONS &addons, const CONTENT_TYPE &content);
    const cp_cfg_element_t *GetExtElement(cp_cfg_element_t *base, const char *path);
    bool GetExtElementDeque(DEQUEELEMENTS &elements, cp_cfg_element_t *base, const char *path);
    cp_context_t *m_cp_context;
    DllLibCPluff *m_cpluff;

    AddonPtr Factory(const cp_extension_t *props);
    bool CheckUserDirs(const cp_cfg_element_t *element);

    // private construction, and no assignements; use the provided singleton methods
    CAddonMgr();
    CAddonMgr(const CAddonMgr&);
    CAddonMgr const& operator=(CAddonMgr const&);
    virtual ~CAddonMgr();

    static std::map<TYPE, IAddonMgrCallback*> m_managers;
    MAPADDONS m_addons;
    CStopWatch m_watch;
    std::map<CStdString, AddonPtr> m_idMap;
    CCriticalSection m_critSection;
  };

}; /* namespace ADDON */
