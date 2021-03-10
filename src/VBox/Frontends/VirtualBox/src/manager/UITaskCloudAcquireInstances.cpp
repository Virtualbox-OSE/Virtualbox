/* $Id: UITaskCloudAcquireInstances.cpp $ */
/** @file
 * VBox Qt GUI - UITaskCloudAcquireInstances class implementation.
 */

/*
 * Copyright (C) 2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/* GUI includes: */
#include "UICloudNetworkingStuff.h"
#include "UITaskCloudAcquireInstances.h"


UITaskCloudAcquireInstances::UITaskCloudAcquireInstances(const CCloudClient &comCloudClient, UIChooserNode *pParentNode)
    : UITask(Type_CloudAcquireInstances)
    , m_comCloudClient(comCloudClient)
    , m_pParentNode(pParentNode)
{
}

QList<UICloudMachine> UITaskCloudAcquireInstances::result() const
{
    m_mutex.lock();
    const QList<UICloudMachine> resultList = m_result;
    m_mutex.unlock();
    return resultList;
}

CVirtualBoxErrorInfo UITaskCloudAcquireInstances::errorInfo()
{
    m_mutex.lock();
    CVirtualBoxErrorInfo comErrorInfo = m_comErrorInfo;
    m_mutex.unlock();
    return comErrorInfo;
}

void UITaskCloudAcquireInstances::run()
{
    m_mutex.lock();
    m_result = listInstances(m_comCloudClient);
    m_mutex.unlock();
}
