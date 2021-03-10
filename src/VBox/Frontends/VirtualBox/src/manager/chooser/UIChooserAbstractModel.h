/* $Id: UIChooserAbstractModel.h $ */
/** @file
 * VBox Qt GUI - UIChooserAbstractModel class declaration.
 */

/*
 * Copyright (C) 2012-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef FEQT_INCLUDED_SRC_manager_chooser_UIChooserAbstractModel_h
#define FEQT_INCLUDED_SRC_manager_chooser_UIChooserAbstractModel_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QThread>

/* GUI includes: */
#include "UIChooserDefs.h"

/* COM includes: */
#include "COMEnums.h"

/* Forward declaration: */
class QUuid;
class UIChooser;
class UIChooserNode;
class CMachine;
#ifdef VBOX_GUI_WITH_CLOUD_VMS
class UITask;
#endif


/** QObject extension used as VM Chooser-pane abstract model.
  * This class is used to load/save a tree of abstract invisible
  * nodes representing VMs and their groups from/to extra-data. */
class UIChooserAbstractModel : public QObject
{
    Q_OBJECT;

signals:

    /** @name Cloud machine stuff.
      * @{ */
        /** Notifies about state change for cloud machine with certain @a strId. */
        void sigCloudMachineStateChange(const QString &strId);
    /** @} */

    /** @name Group saving stuff.
      * @{ */
        /** Commands to start group saving. */
        void sigStartGroupSaving();
        /** Notifies about group saving state changed. */
        void sigGroupSavingStateChanged();
    /** @} */

public:

    /** Constructs abstract Chooser-model passing @a pParent to the base-class. */
    UIChooserAbstractModel(UIChooser *pParent);

    /** @name General stuff.
      * @{ */
        /** Inits model. */
        virtual void init();
        /** Deinits model. */
        virtual void deinit();
    /** @} */

    /** @name Children stuff.
      * @{ */
        /** Returns invisible root node instance. */
        UIChooserNode *invisibleRoot() const;

        /** Wipes out empty groups. */
        void wipeOutEmptyGroups();

        /** Generates unique group name traversing recursively starting from @a pRoot. */
        static QString uniqueGroupName(UIChooserNode *pRoot);
    /** @} */

    /** @name Search stuff.
      * @{ */
        /** Performs a search starting from the m_pInvisibleRootNode. */
        virtual void performSearch(const QString &strSearchTerm, int iItemSearchFlags);
        /** Cleans the search result data members and disables item's visual effects.
          * Also returns a list of all nodes which may be utilized by the calling code. */
        virtual QList<UIChooserNode*> resetSearch();
        /** Returns search result. */
        QList<UIChooserNode*> searchResult() const;
    /** @} */

    /** @name Group saving stuff.
      * @{ */
        /** Commands to save group settings. */
        void saveGroupSettings();
        /** Returns whether group saving is in progress. */
        bool isGroupSavingInProgress() const;
    /** @} */

public slots:

    /** @name Cloud machine stuff.
      * @{ */
        /** Handles cloud machine state change. */
        void sltHandleCloudMachineStateChange();
    /** @} */

    /** @name Group saving stuff.
      * @{ */
        /** Handles group definition saving complete. */
        void sltGroupDefinitionsSaveComplete();
        /** Handles group order saving complete. */
        void sltGroupOrdersSaveComplete();
    /** @} */

protected slots:

    /** @name Main event handling stuff.
      * @{ */
        /** Handles machine @a enmState change for machine with certain @a uMachineId. */
        virtual void sltMachineStateChanged(const QUuid &uMachineId, const KMachineState enmState);
        /** Handles machine data change for machine with certain @a uMachineId. */
        virtual void sltMachineDataChanged(const QUuid &uMachineId);
        /** Handles machine registering/unregistering for machine with certain @a uMachineId. */
        virtual void sltMachineRegistered(const QUuid &uMachineId, const bool fRegistered);
        /** Handles session @a enmState change for machine with certain @a uMachineId. */
        virtual void sltSessionStateChanged(const QUuid &uMachineId, const KSessionState enmState);
        /** Handles snapshot change for machine/snapshot with certain @a uMachineId / @a uSnapshotId. */
        virtual void sltSnapshotChanged(const QUuid &uMachineId, const QUuid &uSnapshotId);
    /** @} */

    /** @name Children stuff.
      * @{ */
        /** Handles reload machine with certain @a uMachineId request. */
        virtual void sltReloadMachine(const QUuid &uMachineId);
    /** @} */

#ifdef VBOX_GUI_WITH_CLOUD_VMS
    /** @name Cloud stuff.
      * @{ */
        /** Handles acquire cloud instances task complete signal. */
        virtual void sltHandleCloudAcquireInstancesTaskComplete(UITask *pTask);
    /** @} */
#endif /* VBOX_GUI_WITH_CLOUD_VMS */

private slots:

    /** @name Group saving stuff.
      * @{ */
        /** Handles request to start group saving. */
        void sltStartGroupSaving();
    /** @} */

private:

    /** @name Prepare/Cleanup cascade.
      * @{ */
        /** Prepares all. */
        void prepare();
        /** Prepares connections. */
        void prepareConnections();
    /** @} */

    /** @name Children stuff.
      * @{ */
        /** Loads tree. */
        void loadTree();
        /** Adds machine item based on certain @a comMachine and optionally @a fMakeItVisible. */
        void addMachineIntoTheTree(const CMachine &comMachine, bool fMakeItVisible = false);
        /** Acquires group node, creates one if necessary.
          * @param  strName           Brings the name of group we looking for.
          * @param  pParentNode       Brings the parent we starting to look for a group from.
          * @param  fAllGroupsOpened  Brings whether we should open all the groups till the required one. */
        UIChooserNode *getGroupNode(const QString &strName, UIChooserNode *pParentNode, bool fAllGroupsOpened);
        /** Returns whether group node with certain @a strName should be opened, searching starting from the passed @a pParentItem. */
        bool shouldGroupNodeBeOpened(UIChooserNode *pParentNode, const QString &strName);

        /** Wipes out empty groups starting from @a pParentItem. */
        void wipeOutEmptyGroupsStartingFrom(UIChooserNode *pParentNode);

        /** Returns whether global node within the @a pParentNode is favorite. */
        bool isGlobalNodeFavorite(UIChooserNode *pParentNode) const;

        /** Acquires desired position for a child of @a pParentNode with specified @a enmType and @a strName. */
        int getDesiredNodePosition(UIChooserNode *pParentNode, UIChooserItemType enmType, const QString &strName);
        /** Acquires defined position for a child of @a pParentNode with specified @a enmType and @a strName. */
        int getDefinedNodePosition(UIChooserNode *pParentNode, UIChooserItemType enmType, const QString &strName);

        /** Creates machine node based on certain @a comMachine as a child of specified @a pParentNode. */
        void createMachineNode(UIChooserNode *pParentNode, const CMachine &comMachine);
    /** @} */

    /** @name Group saving stuff.
      * @{ */
        /** Saves group definitions. */
        void saveGroupDefinitions();
        /** Saves group orders. */
        void saveGroupOrders();

        /** Gathers group @a definitions of @a pParentGroup. */
        void gatherGroupDefinitions(QMap<QString, QStringList> &definitions, UIChooserNode *pParentGroup);
        /** Gathers group @a orders of @a pParentGroup. */
        void gatherGroupOrders(QMap<QString, QStringList> &orders, UIChooserNode *pParentGroup);

        /** Makes sure group definitions saving is finished. */
        void makeSureGroupDefinitionsSaveIsFinished();
        /** Makes sure group orders saving is finished. */
        void makeSureGroupOrdersSaveIsFinished();

        /** Returns QString representation for passed @a uId, wiping out {} symbols.
          * @note  Required for backward compatibility after QString=>QUuid change. */
        static QString toOldStyleUuid(const QUuid &uId);
    /** @} */

    /** @name General stuff.
      * @{ */
        /** Holds the parent widget reference. */
        UIChooser *m_pParent;
    /** @} */

    /** @name Children stuff.
      * @{ */
        /** Holds the invisible root node instance. */
        UIChooserNode *m_pInvisibleRootNode;
    /** @} */

    /** @name Search stuff.
      * @{ */
        /** Stores the results of the current search. */
        QList<UIChooserNode*>  m_searchResults;
    /** @} */

    /** @name Group saving stuff.
      * @{ */
        /** Holds the consolidated map of group definitions/orders. */
        QMap<QString, QStringList>  m_groups;
    /** @} */
};


/** QThread subclass allowing to save group definitions asynchronously. */
class UIThreadGroupDefinitionSave : public QThread
{
    Q_OBJECT;

signals:

    /** Notifies about machine with certain @a uMachineId to be reloaded. */
    void sigReload(const QUuid &uMachineId);

    /** Notifies about task is complete. */
    void sigComplete();

public:

    /** Returns group saving thread instance. */
    static UIThreadGroupDefinitionSave* instance();
    /** Prepares group saving thread instance. */
    static void prepare();
    /** Cleanups group saving thread instance. */
    static void cleanup();

    /** Configures @a groups saving thread with corresponding @a pListener.
      * @param  oldLists  Brings the old definition list to be compared.
      * @param  newLists  Brings the new definition list to be saved. */
    void configure(QObject *pParent,
                   const QMap<QString, QStringList> &oldLists,
                   const QMap<QString, QStringList> &newLists);

protected:

    /** Constructs group saving thread. */
    UIThreadGroupDefinitionSave();
    /** Destructs group saving thread. */
    virtual ~UIThreadGroupDefinitionSave() /* override */;

    /** Contains a thread task to be executed. */
    void run();

    /** Holds the singleton instance. */
    static UIThreadGroupDefinitionSave *s_pInstance;

    /** Holds the map of group definitions to be compared. */
    QMap<QString, QStringList> m_oldLists;
    /** Holds the map of group definitions to be saved. */
    QMap<QString, QStringList> m_newLists;
};


/** QThread subclass allowing to save group order asynchronously. */
class UIThreadGroupOrderSave : public QThread
{
    Q_OBJECT;

signals:

    /** Notifies about task is complete. */
    void sigComplete();

public:

    /** Returns group saving thread instance. */
    static UIThreadGroupOrderSave *instance();
    /** Prepares group saving thread instance. */
    static void prepare();
    /** Cleanups group saving thread instance. */
    static void cleanup();

    /** Configures group saving thread with corresponding @a pListener.
      * @param  groups  Brings the groups to be saved. */
    void configure(QObject *pListener,
                   const QMap<QString, QStringList> &groups);

protected:

    /** Constructs group saving thread. */
    UIThreadGroupOrderSave();
    /** Destructs group saving thread. */
    virtual ~UIThreadGroupOrderSave() /* override */;

    /** Contains a thread task to be executed. */
    virtual void run() /* override */;

    /** Holds the singleton instance. */
    static UIThreadGroupOrderSave *s_pInstance;

    /** Holds the map of groups to be saved. */
    QMap<QString, QStringList>  m_groups;
};


#endif /* !FEQT_INCLUDED_SRC_manager_chooser_UIChooserAbstractModel_h */
