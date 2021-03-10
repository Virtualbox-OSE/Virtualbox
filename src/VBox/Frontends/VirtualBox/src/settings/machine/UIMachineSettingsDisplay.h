/* $Id: UIMachineSettingsDisplay.h $ */
/** @file
 * VBox Qt GUI - UIMachineSettingsDisplay class declaration.
 */

/*
 * Copyright (C) 2008-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsDisplay_h
#define FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsDisplay_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UISettingsPage.h"
#include "UIMachineSettingsDisplay.gen.h"

/* COM includes: */
#include "CGuestOSType.h"

/* Forward declarations: */
class UIActionPool;
struct UIDataSettingsMachineDisplay;
typedef UISettingsCache<UIDataSettingsMachineDisplay> UISettingsCacheMachineDisplay;

/** Machine settings: Display page. */
class SHARED_LIBRARY_STUFF UIMachineSettingsDisplay : public UISettingsPageMachine,
                                                      public Ui::UIMachineSettingsDisplay
{
    Q_OBJECT;

public:

    /** Constructs Display settings page. */
    UIMachineSettingsDisplay();
    /** Destructs Display settings page. */
    ~UIMachineSettingsDisplay();

    /** Defines @a comGuestOSType. */
    void setGuestOSType(CGuestOSType comGuestOSType);

#ifdef VBOX_WITH_3D_ACCELERATION
    /** Returns whether 3D Acceleration is enabled. */
    bool isAcceleration3DSelected() const;
#endif
#ifdef VBOX_WITH_VIDEOHWACCEL
    /** Returns whether 2D Video Acceleration is enabled. */
    bool isAcceleration2DVideoSelected() const;
#endif

    /** Returns recommended graphics controller type. */
    KGraphicsControllerType graphicsControllerTypeRecommended() const;
    /** Returns current graphics controller type. */
    KGraphicsControllerType graphicsControllerTypeCurrent() const;

protected:

    /** Returns whether the page content was changed. */
    virtual bool changed() const /* override */;

    /** Loads data into the cache from corresponding external object(s),
      * this task COULD be performed in other than the GUI thread. */
    virtual void loadToCacheFrom(QVariant &data) /* override */;
    /** Loads data into corresponding widgets from the cache,
      * this task SHOULD be performed in the GUI thread only. */
    virtual void getFromCache() /* override */;

    /** Saves data from corresponding widgets to the cache,
      * this task SHOULD be performed in the GUI thread only. */
    virtual void putToCache() /* override */;
    /** Saves data from the cache to corresponding external object(s),
      * this task COULD be performed in other than the GUI thread. */
    virtual void saveFromCacheTo(QVariant &data) /* overrride */;

    /** Performs validation, updates @a messages list if something is wrong. */
    virtual bool validate(QList<UIValidationMessage> &messages) /* override */;

    /** Defines TAB order for passed @a pWidget. */
    virtual void setOrderAfter(QWidget *pWidget) /* override */;

    /** Handles translation event. */
    virtual void retranslateUi() /* override */;

    /** Performs final page polishing. */
    virtual void polishPage() /* override */;

private slots:

    /** Handles Guest Screen count slider change. */
    void sltHandleGuestScreenCountSliderChange();
    /** Handles Guest Screen count editor change. */
    void sltHandleGuestScreenCountEditorChange();
    /** Handles Graphics Controller combo change. */
    void sltHandleGraphicsControllerComboChange();
#ifdef VBOX_WITH_3D_ACCELERATION
    /** Handles 3D Acceleration check-box change. */
    void sltHandle3DAccelerationCheckboxChange();
#endif
#ifdef VBOX_WITH_VIDEOHWACCEL
    /** Handles 2D Video Acceleration check-box change. */
    void sltHandle2DVideoAccelerationCheckboxChange();
#endif

    /** Handles recording toggle. */
    void sltHandleRecordingCheckboxToggle();
    /** Handles recording frame size change. */
    void sltHandleRecordingVideoFrameSizeComboboxChange();
    /** Handles recording frame width change. */
    void sltHandleRecordingVideoFrameWidthEditorChange();
    /** Handles recording frame height change. */
    void sltHandleRecordingVideoFrameHeightEditorChange();
    /** Handles recording frame rate slider change. */
    void sltHandleRecordingVideoFrameRateSliderChange();
    /** Handles recording frame rate editor change. */
    void sltHandleRecordingVideoFrameRateEditorChange();
    /** Handles recording quality slider change. */
    void sltHandleRecordingVideoQualitySliderChange();
    /** Handles recording bit-rate editor change. */
    void sltHandleRecordingVideoBitRateEditorChange();
    void sltHandleRecordingComboBoxChange();

private:

    /** Prepares all. */
    void prepare();
    /** Prepares 'Screen' tab. */
    void prepareTabScreen();
    /** Prepares 'Remote Display' tab. */
    void prepareTabRemoteDisplay();
    /** Prepares 'Recording' tab. */
    void prepareTabRecording();
    /** Prepares connections. */
    void prepareConnections();
    /** Cleanups all. */
    void cleanup();

    /** Returns whether the VRAM requirements are important. */
    bool shouldWeWarnAboutLowVRAM();

    /** Searches for corresponding frame size preset. */
    void lookForCorrespondingFrameSizePreset();
    /** Updates guest-screen count. */
    void updateGuestScreenCount();
    /** Updates recording file size hint. */
    void updateRecordingFileSizeHint();
    /** Searches for the @a data field in corresponding @a pComboBox. */
    static void lookForCorrespondingPreset(QComboBox *pComboBox, const QVariant &data);
    /** Calculates recording video bit-rate for passed @a iFrameWidth, @a iFrameHeight, @a iFrameRate and @a iQuality. */
    static int calculateBitRate(int iFrameWidth, int iFrameHeight, int iFrameRate, int iQuality);
    /** Calculates recording video quality for passed @a iFrameWidth, @a iFrameHeight, @a iFrameRate and @a iBitRate. */
    static int calculateQuality(int iFrameWidth, int iFrameHeight, int iFrameRate, int iBitRate);
    /** Saves existing display data from the cache. */
    bool saveDisplayData();
    /** Saves existing 'Screen' data from the cache. */
    bool saveScreenData();
    /** Saves existing 'Remote Display' data from the cache. */
    bool saveRemoteDisplayData();
    /** Saves existing 'Recording' data from the cache. */
    bool saveRecordingData();
    /** Decide which of the recording related widgets are to be disabled/enabled. */
    void enableDisableRecordingWidgets();

    /** Holds the guest OS type ID. */
    CGuestOSType  m_comGuestOSType;
#ifdef VBOX_WITH_3D_ACCELERATION
    /** Holds whether the guest OS supports WDDM. */
    bool          m_fWddmModeSupported;
#endif
#ifdef VBOX_WITH_VIDEOHWACCEL
    /** Holds whether the guest OS supports 2D Video Acceleration. */
    bool          m_f2DVideoAccelerationSupported;
#endif
    /** Holds recommended graphics controller type. */
    KGraphicsControllerType  m_enmGraphicsControllerTypeRecommended;

    /** Holds the page data cache instance. */
    UISettingsCacheMachineDisplay *m_pCache;
};

#endif /* !FEQT_INCLUDED_SRC_settings_machine_UIMachineSettingsDisplay_h */
