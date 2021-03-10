/* $Id: UIInformationRuntime.cpp $ */
/** @file
 * VBox Qt GUI - UIInformationRuntime class implementation.
 */

/*
 * Copyright (C) 2016-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/* Qt includes: */
#include <QApplication>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QTableWidget>
#include <QTimer>

/* GUI includes: */
#include "UICommon.h"
#include "UIConverter.h"
#include "UIIconPool.h"
#include "UIInformationRuntime.h"
#include "UISession.h"

/* COM includes: */
#include "CGraphicsAdapter.h"
#include "CGuest.h"
#include "CVRDEServerInfo.h"

enum InfoRow
{
    InfoRow_Title = 0,
    InfoRow_Resolution,
    InfoRow_Uptime,
    InfoRow_ClipboardMode,
    InfoRow_DnDMode,
    InfoRow_ExecutionEngine,
    InfoRow_NestedPaging,
    InfoRow_UnrestrictedExecution,
    InfoRow_Paravirtualization,
    InfoRow_GuestAdditions,
    InfoRow_GuestOSType,
    InfoRow_RemoteDesktop,
    InfoRow_Max
};

/*********************************************************************************************************************************
*   UIRuntimeInfoWidget definition.                                                                                     *
*********************************************************************************************************************************/
/** A QTablWidget extention to show some runtime attributes. Some of these are updated in response to IConsole events. Uptime field
  * is updated thru a QTimer. */
class UIRuntimeInfoWidget : public QIWithRetranslateUI<QTableWidget>
{

    Q_OBJECT;

public:

    UIRuntimeInfoWidget(QWidget *pParent, const CMachine &machine, const CConsole &console);
    void updateScreenInfo(int iScreenId = -1);
    void updateGAsVersion();
    void updateVRDE();
    void updateClipboardMode(KClipboardMode enmMode = KClipboardMode_Max);
    void updateDnDMode(KDnDMode enmMode = KDnDMode_Max);

protected:

    virtual void retranslateUi() /* override */;

private slots:

    void sltTimeout();

private:

    void createInfoRows();
    void updateUpTime();
    void updateTitleRow();
    void updateOSTypeRow();
    void updateVirtualizationInfo();

    /** Searches the table for the @p item of enmLine and replaces its text. if not found inserts a new
      * row to the end of the table. Assumes only one line of the @p enmLine exists. */
    void updateInfoRow(InfoRow enmLine, const QString &strColumn0, const QString &strColumn1);
    QString screenResolution(int iScreenId);
    /** Creates to QTableWidgetItems of the @enmInfoRow using the @p strLabel and @p strInfo and inserts it
     * to the row @p iRow. If @p iRow is -1 then the items inserted to the end of the table. */
    void insertInfoRow(InfoRow enmInfoRow, const QString& strLabel, const QString &strInfo, int iRow = -1);
    void computeMinimumWidth();

    CMachine m_machine;
    CConsole m_console;

    /** @name Cached translated strings.
      * @{ */
        QString m_strTableTitle;
        QString m_strScreenResolutionLabel;
        QString m_strMonitorTurnedOff;
        QString m_strUptimeLabel;
        QString m_strClipboardModeLabel;
        QString m_strDragAndDropLabel;
        QString m_strExcutionEngineLabel;
        QString m_strNestedPagingLabel;
        QString m_strUnrestrictedExecutionLabel;
        QString m_strParavirtualizationLabel;
        QString m_strNestedPagingActive;
        QString m_strNestedPagingInactive;
        QString m_strUnrestrictedExecutionActive;
        QString m_strUnrestrictedExecutionInactive;
        QString m_strVRDEPortNotAvailable;
        QString m_strGuestAdditionsLabel;
        QString m_strGuestOSTypeLabel;
        QString m_strRemoteDesktopLabel;
        QString m_strExecutionEngineNotSet;
        QString m_strOSNotDetected;
        QString m_strGANotDetected;
    /** @} */

    int m_iFontHeight;
    /** Computed by computing the maximum length line. Used to avoid having horizontal scroll bars. */
    int m_iMinimumWidth;
    QVector<QString> m_screenResolutions;
    QVector<QString*> m_labels;
    QTimer *m_pTimer;
};

/*********************************************************************************************************************************
*   UIRuntimeInfoWidget implementation.                                                                                     *
*********************************************************************************************************************************/

UIRuntimeInfoWidget::UIRuntimeInfoWidget(QWidget *pParent, const CMachine &machine, const CConsole &console)
    : QIWithRetranslateUI<QTableWidget>(pParent)
    , m_machine(machine)
    , m_console(console)
    , m_iMinimumWidth(0)
    , m_pTimer(0)
{
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAlternatingRowColors(true);
    m_iFontHeight = QFontMetrics(font()).height();

    setColumnCount(2);
    verticalHeader()->hide();
    horizontalHeader()->hide();
    setShowGrid(false);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setFocusPolicy(Qt::NoFocus);
    setSelectionMode(QAbstractItemView::NoSelection);

    m_pTimer = new QTimer(this);
    if (m_pTimer)
    {
        connect(m_pTimer, &QTimer::timeout, this, &UIRuntimeInfoWidget::sltTimeout);
        m_pTimer->start(5000);
    }

    m_labels << &m_strScreenResolutionLabel
             << &m_strUptimeLabel
             << &m_strDragAndDropLabel
             << &m_strExcutionEngineLabel
             << &m_strNestedPagingLabel
             << &m_strUnrestrictedExecutionLabel
             << &m_strParavirtualizationLabel
             << &m_strGuestAdditionsLabel
             << &m_strGuestOSTypeLabel
             << &m_strRemoteDesktopLabel;


    retranslateUi();
    computeMinimumWidth();
}

void UIRuntimeInfoWidget::retranslateUi()
{
    m_strTableTitle                    = QApplication::translate("UIVMInformationDialog", "Runtime Attributes");
    m_strScreenResolutionLabel         = QApplication::translate("UIVMInformationDialog", "Screen Resolution");
    m_strMonitorTurnedOff              = QApplication::translate("UIVMInformationDialog", "turned off", "Screen");
    m_strUptimeLabel                   = QApplication::translate("UIVMInformationDialog", "VM Uptime");
    m_strClipboardModeLabel            = QApplication::translate("UIVMInformationDialog", "Clipboard Mode");
    m_strDragAndDropLabel              = QApplication::translate("UIVMInformationDialog", "Drag and Drop Mode");
    m_strExcutionEngineLabel           = QApplication::translate("UIVMInformationDialog", "VM Execution Engine");
    m_strNestedPagingLabel             = QApplication::translate("UIVMInformationDialog", "Nested Paging");
    m_strUnrestrictedExecutionLabel    = QApplication::translate("UIVMInformationDialog", "Unrestricted Execution");
    m_strParavirtualizationLabel       = QApplication::translate("UIVMInformationDialog", "Paravirtualization Interface");
    m_strNestedPagingActive            = QApplication::translate("UIVMInformationDialog", "Active", "Nested Paging");
    m_strNestedPagingInactive          = QApplication::translate("UIVMInformationDialog", "Inactive", "Nested Paging");
    m_strUnrestrictedExecutionActive   = QApplication::translate("UIVMInformationDialog", "Active", "Unrestricted Execution");
    m_strUnrestrictedExecutionInactive = QApplication::translate("UIVMInformationDialog", "Inactive", "Unrestricted Execution");
    m_strVRDEPortNotAvailable          = QApplication::translate("UIVMInformationDialog", "Not Available", "VRDE Port");
    m_strGuestAdditionsLabel           = QApplication::translate("UIVMInformationDialog", "Guest Additions");
    m_strGuestOSTypeLabel              = QApplication::translate("UIVMInformationDialog", "Guest OS Type");
    m_strRemoteDesktopLabel            = QApplication::translate("UIVMInformationDialog", "Remote Desktop Server Port");
    m_strExecutionEngineNotSet         = QApplication::translate("UIVMInformationDialog", "not set", "Execution Engine");
    m_strOSNotDetected                 = QApplication::translate("UIVMInformationDialog", "Not Detected", "Guest OS Type");
    m_strGANotDetected                 = QApplication::translate("UIVMInformationDialog", "Not Detected", "Guest Additions Version");

    QString* strLongest = 0;
    foreach (QString *strLabel, m_labels)
    {
        if (!strLongest)
            strLongest = strLabel;
        if (strLabel && strLongest->length() < strLabel->length())
            strLongest = strLabel;
    }
    QFontMetrics fontMetrics(font());
    setColumnWidth(0, 1.5 * fontMetrics.width(*strLongest));

    /* Make the API calls and populate the table: */
    createInfoRows();
}

void UIRuntimeInfoWidget::insertInfoRow(InfoRow enmInfoRow, const QString& strLabel, const QString &strInfo, int iRow /* = -1 */)
{
    int iNewRow = rowCount();
    if (iRow != -1 && iRow <= iNewRow)
        iNewRow = iRow;
    insertRow(iNewRow);
    setItem(iNewRow, 1, new QTableWidgetItem(strLabel, enmInfoRow));
    setItem(iNewRow, 2, new QTableWidgetItem(strInfo, enmInfoRow));
    int iMargin = 0.2 * qApp->style()->pixelMetric(QStyle::PM_LayoutTopMargin);
    setRowHeight(iNewRow, 2 * iMargin + m_iFontHeight);
}

QString UIRuntimeInfoWidget::screenResolution(int iScreenID)
{
    /* Determine resolution: */
    ULONG uWidth = 0;
    ULONG uHeight = 0;
    ULONG uBpp = 0;
    LONG xOrigin = 0;
    LONG yOrigin = 0;
    KGuestMonitorStatus monitorStatus = KGuestMonitorStatus_Enabled;
    m_console.GetDisplay().GetScreenResolution(iScreenID, uWidth, uHeight, uBpp, xOrigin, yOrigin, monitorStatus);
    QString strResolution = QString("%1x%2").arg(uWidth).arg(uHeight);
    if (uBpp)
        strResolution += QString("x%1").arg(uBpp);
    strResolution += QString(" @%1,%2").arg(xOrigin).arg(yOrigin);
    if (monitorStatus == KGuestMonitorStatus_Disabled)
    {
        strResolution += QString(" ");
        strResolution += m_strMonitorTurnedOff;
    }
    return strResolution;
}

void UIRuntimeInfoWidget::sltTimeout()
{
    updateUpTime();
}

void UIRuntimeInfoWidget::updateScreenInfo(int iScreenID /* = -1 */)
{
    ULONG uGuestScreens = m_machine.GetGraphicsAdapter().GetMonitorCount();
    m_screenResolutions.resize(uGuestScreens);
    if (iScreenID != -1 && iScreenID >= (int)uGuestScreens)
        return;
    if (iScreenID == -1)
    {
        for (ULONG iScreen = 0; iScreen < uGuestScreens; ++iScreen)
            m_screenResolutions[iScreen] = screenResolution(iScreen);
    }
    else
        m_screenResolutions[iScreenID] = screenResolution(iScreenID);
    /* Delete all the rows (not only the updated screen's row) and reinsert them: */
    int iRowCount = rowCount();
    for (int i = iRowCount - 1; i >= 0; --i)
    {
        QTableWidgetItem *pItem = item(i, 1);
        if (pItem && pItem->type() == InfoRow_Resolution)
            removeRow(i);
    }
    for (ULONG iScreen = 0; iScreen < uGuestScreens; ++iScreen)
    {
        QString strLabel = uGuestScreens > 1 ?
            QString("%1 %2").arg(m_strScreenResolutionLabel).arg(QString::number(iScreen)) :
            QString("%1").arg(m_strScreenResolutionLabel);
        /* Insert the screen resolution row at the top of the table. Row 0 is the title row: */
        insertInfoRow(InfoRow_Resolution, strLabel, m_screenResolutions[iScreen], iScreen + 1);
    }
    resizeColumnToContents(1);
    horizontalHeader()->setStretchLastSection(true);
}

void UIRuntimeInfoWidget::updateUpTime()
{
    CMachineDebugger debugger = m_console.GetDebugger();
    uint32_t uUpSecs = (debugger.GetUptime() / 5000) * 5;
    char szUptime[32];
    uint32_t uUpDays = uUpSecs / (60 * 60 * 24);
    uUpSecs -= uUpDays * 60 * 60 * 24;
    uint32_t uUpHours = uUpSecs / (60 * 60);
    uUpSecs -= uUpHours * 60 * 60;
    uint32_t uUpMins  = uUpSecs / 60;
    uUpSecs -= uUpMins * 60;
    RTStrPrintf(szUptime, sizeof(szUptime), "%dd %02d:%02d:%02d",
                uUpDays, uUpHours, uUpMins, uUpSecs);
    QString strUptime = QString(szUptime);
    updateInfoRow(InfoRow_Uptime, QString("%1").arg(m_strUptimeLabel), strUptime);
}

void UIRuntimeInfoWidget::updateTitleRow()
{
    /* Add the title row always as 0th row: */
    QTableWidgetItem *pTitleIcon = new QTableWidgetItem(UIIconPool::iconSet(":/state_running_16px.png"), "", InfoRow_Title);
    QTableWidgetItem *pTitleItem = new QTableWidgetItem(m_strTableTitle, InfoRow_Title);
    QFont titleFont(font());
    titleFont.setBold(true);
    pTitleItem->setFont(titleFont);
    if (rowCount() < 1)
        insertRow(0);
    setItem(0, 0, pTitleIcon);
    setItem(0, 1, pTitleItem);
    resizeColumnToContents(0);
}

void UIRuntimeInfoWidget::updateOSTypeRow()
{
   QString strOSType = m_console.GetGuest().GetOSTypeId();
    if (strOSType.isEmpty())
        strOSType = m_strOSNotDetected;
    else
        strOSType = uiCommon().vmGuestOSTypeDescription(strOSType);
   updateInfoRow(InfoRow_GuestOSType, QString("%1").arg(m_strGuestOSTypeLabel), strOSType);
}

void UIRuntimeInfoWidget::updateVirtualizationInfo()
{

    /* Determine virtualization attributes: */
    CMachineDebugger debugger = m_console.GetDebugger();

    QString strExecutionEngine;
    switch (debugger.GetExecutionEngine())
    {
        case KVMExecutionEngine_HwVirt:
            strExecutionEngine = "VT-x/AMD-V";  /* no translation */
            break;
        case KVMExecutionEngine_RawMode:
            strExecutionEngine = "raw-mode";    /* no translation */
            break;
        case KVMExecutionEngine_NativeApi:
            strExecutionEngine = "native API";  /* no translation */
            break;
        default:
            AssertFailed();
            RT_FALL_THRU();
        case KVMExecutionEngine_NotSet:
            strExecutionEngine = m_strExecutionEngineNotSet;
            break;
    }
    QString strNestedPaging = debugger.GetHWVirtExNestedPagingEnabled() ?
        m_strNestedPagingActive : m_strNestedPagingInactive;
    QString strUnrestrictedExecution = debugger.GetHWVirtExUXEnabled() ?
        m_strUnrestrictedExecutionActive : m_strUnrestrictedExecutionInactive;
    QString strParavirtProvider = gpConverter->toString(m_machine.GetEffectiveParavirtProvider());

    updateInfoRow(InfoRow_ExecutionEngine, QString("%1").arg(m_strExcutionEngineLabel), strExecutionEngine);
    updateInfoRow(InfoRow_NestedPaging, QString("%1").arg(m_strNestedPagingLabel), strNestedPaging);
    updateInfoRow(InfoRow_UnrestrictedExecution, QString("%1").arg(m_strUnrestrictedExecutionLabel), strUnrestrictedExecution);
    updateInfoRow(InfoRow_Paravirtualization, QString("%1").arg(m_strParavirtualizationLabel), strParavirtProvider);
}

void UIRuntimeInfoWidget::updateGAsVersion()
{
    CGuest guest = m_console.GetGuest();
    QString strGAVersion = guest.GetAdditionsVersion();
    if (strGAVersion.isEmpty())
        strGAVersion = m_strGANotDetected;
    else
    {
        ULONG uRevision = guest.GetAdditionsRevision();
        if (uRevision != 0)
            strGAVersion += QString(" r%1").arg(uRevision);
    }
   updateInfoRow(InfoRow_GuestAdditions, QString("%1").arg(m_strGuestAdditionsLabel), strGAVersion);
}

void UIRuntimeInfoWidget::updateVRDE()
{
    int iVRDEPort = m_console.GetVRDEServerInfo().GetPort();
    QString strVRDEInfo = (iVRDEPort == 0 || iVRDEPort == -1) ?
        m_strVRDEPortNotAvailable : QString("%1").arg(iVRDEPort);
   updateInfoRow(InfoRow_RemoteDesktop, QString("%1").arg(m_strRemoteDesktopLabel), strVRDEInfo);
}

void UIRuntimeInfoWidget::updateClipboardMode(KClipboardMode enmMode /* = KClipboardMode_Max */)
{
    if (enmMode == KClipboardMode_Max)
        updateInfoRow(InfoRow_ClipboardMode, QString("%1").arg(m_strClipboardModeLabel),
                      gpConverter->toString(m_machine.GetClipboardMode()));
    else
        updateInfoRow(InfoRow_ClipboardMode, QString("%1").arg(m_strClipboardModeLabel),
                      gpConverter->toString(enmMode));
}

void UIRuntimeInfoWidget::updateDnDMode(KDnDMode enmMode /* = KDnDMode_Max */)
{
    if (enmMode == KDnDMode_Max)
        updateInfoRow(InfoRow_DnDMode, QString("%1").arg(m_strDragAndDropLabel),
                  gpConverter->toString(m_machine.GetDnDMode()));
    else
        updateInfoRow(InfoRow_DnDMode, QString("%1").arg(m_strDragAndDropLabel),
                      gpConverter->toString(enmMode));
}

void UIRuntimeInfoWidget::updateInfoRow(InfoRow enmLine, const QString &strColumn0, const QString &strColumn1)
{
    QTableWidgetItem *pItem = 0;
    for (int i = 0; i < rowCount() && !pItem; ++i)
    {
        pItem = item(i, 2);
        if (!pItem)
            continue;
        if (pItem->type() != enmLine)
            pItem = 0;
    }
    if (!pItem)
        insertInfoRow(enmLine, strColumn0, strColumn1);
    else
        pItem->setText(strColumn1);
}

void UIRuntimeInfoWidget::createInfoRows()
{
    clear();
    setRowCount(0);
    setColumnCount(3);
    updateTitleRow();
    updateScreenInfo();
    updateUpTime();
    updateClipboardMode();
    updateDnDMode();
    updateVirtualizationInfo();
    updateGAsVersion();
    updateOSTypeRow();
    updateVRDE();
    resizeColumnToContents(1);
}

void UIRuntimeInfoWidget::computeMinimumWidth()
{
    m_iMinimumWidth = 0;
    for (int j = 0; j < columnCount(); ++j)
        m_iMinimumWidth += columnWidth(j);
}



/*********************************************************************************************************************************
*   UIInformationRuntime implementation.                                                                                     *
*********************************************************************************************************************************/

UIInformationRuntime::UIInformationRuntime(QWidget *pParent, const CMachine &machine, const CConsole &console, const UISession *pSession)
    : QIWithRetranslateUI<QWidget>(pParent)
    , m_machine(machine)
    , m_console(console)
    , m_pMainLayout(0)
    , m_pRuntimeInfoWidget(0)
{
    if (!m_console.isNull())
        m_comGuest = m_console.GetGuest();
    connect(pSession, &UISession::sigAdditionsStateChange, this, &UIInformationRuntime::sltGuestAdditionsStateChange);
    connect(pSession, &UISession::sigGuestMonitorChange, this, &UIInformationRuntime::sltGuestMonitorChange);
    connect(pSession, &UISession::sigVRDEChange, this, &UIInformationRuntime::sltVRDEChange);
    connect(pSession, &UISession::sigClipboardModeChange, this, &UIInformationRuntime::sltClipboardChange);
    connect(pSession, &UISession::sigDnDModeChange, this, &UIInformationRuntime::sltDnDModeChange);

    prepareObjects();
    retranslateUi();
}

void UIInformationRuntime::retranslateUi()
{
}

void UIInformationRuntime::prepareObjects()
{
    m_pMainLayout = new QVBoxLayout(this);
    if (!m_pMainLayout)
        return;
    m_pMainLayout->setSpacing(0);

    m_pRuntimeInfoWidget = new UIRuntimeInfoWidget(0, m_machine, m_console);
    m_pMainLayout->addWidget(m_pRuntimeInfoWidget);
    m_pRuntimeInfoWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
}

void UIInformationRuntime::sltGuestAdditionsStateChange()
{
    if (m_pRuntimeInfoWidget)
        m_pRuntimeInfoWidget->updateGAsVersion();
}

void UIInformationRuntime::sltGuestMonitorChange(KGuestMonitorChangedEventType changeType, ulong uScreenId, QRect screenGeo)
{
    Q_UNUSED(changeType);
    Q_UNUSED(screenGeo);
    if (m_pRuntimeInfoWidget)
        m_pRuntimeInfoWidget->updateScreenInfo(uScreenId);
}

void UIInformationRuntime::sltVRDEChange()
{
    if (m_pRuntimeInfoWidget)
        m_pRuntimeInfoWidget->updateVRDE();
}

void UIInformationRuntime::sltClipboardChange(KClipboardMode enmMode)
{
    if (m_pRuntimeInfoWidget)
        m_pRuntimeInfoWidget->updateClipboardMode(enmMode);
}

void UIInformationRuntime::sltDnDModeChange(KDnDMode enmMode)
{
    if (m_pRuntimeInfoWidget)
        m_pRuntimeInfoWidget->updateDnDMode(enmMode);
}

#include "UIInformationRuntime.moc"
