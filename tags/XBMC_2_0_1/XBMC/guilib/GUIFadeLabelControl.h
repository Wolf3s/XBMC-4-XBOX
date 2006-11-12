/*!
\file GUIFadeLabelControl.h
\brief 
*/

#ifndef GUILIB_GUIFADELABELCONTROL_H
#define GUILIB_GUIFADELABELCONTROL_H

#pragma once

#include "GUIControl.h"

#include "GUILabelControl.h"  // for CInfoPortion

/*!
 \ingroup controls
 \brief 
 */
class CGUIFadeLabelControl : public CGUIControl
{
public:
  CGUIFadeLabelControl(DWORD dwParentID, DWORD dwControlId, int iPosX, int iPosY, DWORD dwWidth, DWORD dwHeight, const CLabelInfo& labelInfo);
  virtual ~CGUIFadeLabelControl(void);
  virtual void Render();
  virtual bool CanFocus() const;
  virtual bool OnMessage(CGUIMessage& message);
  const CLabelInfo& GetLabelInfo() const { return m_label; };

  void SetInfo(const vector<int> &vecInfo);
  void SetLabel(const vector<string> &vecLabel);
  const vector<int> &GetInfo() const { return m_vecInfo; };
  const vector<string> &GetLabel() const { return m_stringLabels; };

protected:
  void AddLabel(const string &label);
  void RenderText(float fPosX, float fPosY, float fMaxWidth, DWORD dwTextColor, WCHAR* wszText, bool bScroll );

  vector<string> m_stringLabels;
  vector< vector<CInfoPortion> > m_infoLabels;

  CLabelInfo m_label;
  int m_iCurrentLabel;
  bool m_bFadeIn;
  int m_iCurrentFrame;
  vector<int> m_vecInfo;
  CScrollInfo m_scrollInfo;
};
#endif
