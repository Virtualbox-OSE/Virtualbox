/* $Id: tstClipboardTransfers.cpp $ */
/** @file
 * Shared Clipboard transfers test case.
 */

/*
 * Copyright (C) 2019-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include "../VBoxSharedClipboardSvc-internal.h"

#include <VBox/HostServices/VBoxClipboardSvc.h>

#include <iprt/assert.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/test.h>


static int testCreateTempDir(RTTEST hTest, const char *pszTestcase, char *pszTempDir, size_t cbTempDir)
{
    char szTempDir[RTPATH_MAX];
    int rc = RTPathTemp(szTempDir, sizeof(szTempDir));
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    rc = RTPathAppend(szTempDir, sizeof(szTempDir), "tstClipboardTransfers");
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    rc = RTDirCreate(szTempDir, 0700, 0);
    if (rc == VERR_ALREADY_EXISTS)
        rc = VINF_SUCCESS;
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    rc = RTPathAppend(szTempDir, sizeof(szTempDir), "XXXXX");
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    rc = RTDirCreateTemp(szTempDir, 0700);
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    rc = RTPathJoin(pszTempDir, cbTempDir, szTempDir, pszTestcase);
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    RTTestPrintf(hTest, RTTESTLVL_DEBUG, "Created temporary directory: %s\n", pszTempDir);

    return rc;
}

static int testRemoveTempDir(RTTEST hTest)
{
    char szTempDir[RTPATH_MAX];
    int rc = RTPathTemp(szTempDir, sizeof(szTempDir));
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    rc = RTPathAppend(szTempDir, sizeof(szTempDir), "tstClipboardTransfers");
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    rc = RTDirRemoveRecursive(szTempDir, RTDIRRMREC_F_CONTENT_AND_DIR);
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    RTTestPrintf(hTest, RTTESTLVL_DEBUG, "Removed temporary directory: %s\n", szTempDir);

    return rc;
}

static int testCreateDir(RTTEST hTest, const char *pszPathToCreate)
{
    RTTestPrintf(hTest, RTTESTLVL_DEBUG, "Creating directory: %s\n", pszPathToCreate);

    int rc = RTDirCreateFullPath(pszPathToCreate, 0700);
    if (rc == VERR_ALREADY_EXISTS)
        rc = VINF_SUCCESS;
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);

    return rc;
}

static int testCreateFile(RTTEST hTest, const char *pszTempDir, const char *pszFileName, uint32_t fOpen, size_t cbSize,
                          char **ppszFilePathAbs)
{
    char szFilePath[RTPATH_MAX];

    int rc = RTStrCopy(szFilePath, sizeof(szFilePath), pszTempDir);
    RTTESTI_CHECK_RC_OK_RET(rc, rc);

    rc = RTPathAppend(szFilePath, sizeof(szFilePath), pszFileName);
    RTTESTI_CHECK_RC_OK_RET(rc, rc);

    char *pszDirToCreate = RTStrDup(szFilePath);
    RTTESTI_CHECK_RET(pszDirToCreate, VERR_NO_MEMORY);

    RTPathStripFilename(pszDirToCreate);

    rc = testCreateDir(hTest, pszDirToCreate);
    RTTESTI_CHECK_RC_OK_RET(rc, rc);

    RTStrFree(pszDirToCreate);
    pszDirToCreate = NULL;

    if (!fOpen)
        fOpen = RTFILE_O_OPEN_CREATE | RTFILE_O_WRITE | RTFILE_O_DENY_NONE;

    RTTestPrintf(hTest, RTTESTLVL_DEBUG, "Creating file: %s\n", szFilePath);

    RTFILE hFile;
    rc = RTFileOpen(&hFile, szFilePath, fOpen);
    if (RT_SUCCESS(rc))
    {
        if (cbSize)
        {
            /** @todo Fill in some random stuff. */
        }

        rc = RTFileClose(hFile);
        RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);
    }

    if (ppszFilePathAbs)
        *ppszFilePathAbs = RTStrDup(szFilePath);

    return rc;
}

typedef struct TESTTRANSFERROOTENTRY
{
    TESTTRANSFERROOTENTRY(const RTCString &a_strPath)
        : strPath(a_strPath) { }

    RTCString strPath;
} TESTTRANSFERROOTENTRY;

static int testAddRootEntry(RTTEST hTest, const char *pszTempDir,
                            const TESTTRANSFERROOTENTRY &rootEntry, char **ppszRoots)
{
    char *pszRoots = NULL;

    const char *pszPath = rootEntry.strPath.c_str();

    char *pszPathAbs;
    int rc = testCreateFile(hTest, pszTempDir, pszPath, 0, 0, &pszPathAbs);
    RTTESTI_CHECK_RC_OK_RET(rc, rc);

    rc = RTStrAAppend(&pszRoots, pszPathAbs);
    RTTESTI_CHECK_RC_OK(rc);

    rc = RTStrAAppend(&pszRoots, "\r\n");
    RTTESTI_CHECK_RC_OK(rc);

    RTStrFree(pszPathAbs);

    *ppszRoots = pszRoots;

    return rc;
}

static int testAddRootEntries(RTTEST hTest, const char *pszTempDir,
                              RTCList<TESTTRANSFERROOTENTRY> &lstBase, RTCList<TESTTRANSFERROOTENTRY> lstToExtend,
                              char **ppszRoots)
{
    int rc = VINF_SUCCESS;

    char *pszRoots = NULL;

    for (size_t i = 0; i < lstBase.size(); ++i)
    {
        char *pszEntry = NULL;
        rc = testAddRootEntry(hTest, pszTempDir, lstBase.at(i), &pszEntry);
        RTTESTI_CHECK_RC_OK_BREAK(rc);
        rc = RTStrAAppend(&pszRoots, pszEntry);
        RTTESTI_CHECK_RC_OK_BREAK(rc);
        RTStrFree(pszEntry);
    }

    for (size_t i = 0; i < lstToExtend.size(); ++i)
    {
        char *pszEntry = NULL;
        rc = testAddRootEntry(hTest, pszTempDir, lstToExtend.at(i), &pszEntry);
        RTTESTI_CHECK_RC_OK_BREAK(rc);
        rc = RTStrAAppend(&pszRoots, pszEntry);
        RTTESTI_CHECK_RC_OK_BREAK(rc);
        RTStrFree(pszEntry);
    }

    if (RT_SUCCESS(rc))
        *ppszRoots = pszRoots;

    return rc;
}

static void testTransferRootsSetSingle(RTTEST hTest,
                                       RTCList<TESTTRANSFERROOTENTRY> &lstBase, RTCList<TESTTRANSFERROOTENTRY> lstToExtend,
                                       int rcExpected)
{
    PSHCLTRANSFER pTransfer;
    int rc = ShClTransferCreate(&pTransfer);
    RTTESTI_CHECK_RC_OK(rc);

    char szTestTransferRootsSetDir[RTPATH_MAX];
    rc = testCreateTempDir(hTest, "testTransferRootsSet", szTestTransferRootsSetDir, sizeof(szTestTransferRootsSetDir));
    RTTESTI_CHECK_RC_OK_RETV(rc);

    /* This is the file we're trying to access (but not supposed to). */
    rc = testCreateFile(hTest, szTestTransferRootsSetDir, "must-not-access-this", 0, 0, NULL);
    RTTESTI_CHECK_RC_OK(rc);

    char *pszRoots;
    rc = testAddRootEntries(hTest, szTestTransferRootsSetDir, lstBase, lstToExtend, &pszRoots);
    RTTESTI_CHECK_RC_OK_RETV(rc);

    rc = ShClTransferRootsSet(pTransfer, pszRoots, strlen(pszRoots) + 1);
    RTTESTI_CHECK_RC(rc, rcExpected);

    RTStrFree(pszRoots);

    rc = ShClTransferDestroy(pTransfer);
    RTTESTI_CHECK_RC_OK(rc);
}

static void testTransferObjOpenSingle(RTTEST hTest,
                                      RTCList<TESTTRANSFERROOTENTRY> &lstRoots, const char *pszObjPath, int rcExpected)
{
    RT_NOREF(hTest);

    PSHCLTRANSFER pTransfer;
    int rc = ShClTransferCreate(&pTransfer);
    RTTESTI_CHECK_RC_OK(rc);

    rc = ShClTransferInit(pTransfer, 0 /* ID */, SHCLTRANSFERDIR_FROM_REMOTE, SHCLSOURCE_LOCAL);
    RTTESTI_CHECK_RC_OK(rc);

    char szTestTransferObjOpenDir[RTPATH_MAX];
    rc = testCreateTempDir(hTest, "testTransferObjOpen", szTestTransferObjOpenDir, sizeof(szTestTransferObjOpenDir));
    RTTESTI_CHECK_RC_OK_RETV(rc);

    /* This is the file we're trying to access (but not supposed to). */
    rc = testCreateFile(hTest, szTestTransferObjOpenDir, "file1.txt", 0, 0, NULL);
    RTTESTI_CHECK_RC_OK(rc);

    RTCList<TESTTRANSFERROOTENTRY> lstToExtendEmpty;

    char *pszRoots;
    rc = testAddRootEntries(hTest, szTestTransferObjOpenDir, lstRoots, lstToExtendEmpty, &pszRoots);
    RTTESTI_CHECK_RC_OK_RETV(rc);

    rc = ShClTransferRootsSet(pTransfer, pszRoots, strlen(pszRoots) + 1);
    RTTESTI_CHECK_RC_OK(rc);

    RTStrFree(pszRoots);

    SHCLOBJOPENCREATEPARMS openCreateParms;
    rc = ShClTransferObjOpenParmsInit(&openCreateParms);
    RTTESTI_CHECK_RC_OK(rc);

    rc = RTStrCopy(openCreateParms.pszPath, openCreateParms.cbPath, pszObjPath);
    RTTESTI_CHECK_RC_OK(rc);

    SHCLOBJHANDLE hObj;
    rc = ShClTransferObjOpen(pTransfer, &openCreateParms, &hObj);
    RTTESTI_CHECK_RC(rc, rcExpected);
    if (RT_SUCCESS(rc))
    {
        rc = ShClTransferObjClose(pTransfer, hObj);
        RTTESTI_CHECK_RC_OK(rc);
    }

    rc = ShClTransferDestroy(pTransfer);
    RTTESTI_CHECK_RC_OK(rc);
}

static void testTransferRootsSet(RTTEST hTest)
{
    RTTestISub("Testing setting transfer roots");

    /* Define the (valid) transfer root set. */
    RTCList<TESTTRANSFERROOTENTRY> lstBase;
    lstBase.append(TESTTRANSFERROOTENTRY("my-transfer-1/file1.txt"));
    lstBase.append(TESTTRANSFERROOTENTRY("my-transfer-1/dir1/file1.txt"));
    lstBase.append(TESTTRANSFERROOTENTRY("my-transfer-1/dir1/sub1/file1.txt"));
    lstBase.append(TESTTRANSFERROOTENTRY("my-transfer-1/dir2/file1.txt"));
    lstBase.append(TESTTRANSFERROOTENTRY("my-transfer-1/dir2/sub1/file1.txt"));

    RTCList<TESTTRANSFERROOTENTRY> lstBreakout;
    testTransferRootsSetSingle(hTest, lstBase, lstBreakout, VINF_SUCCESS);

    lstBreakout.clear();
    lstBase.append(TESTTRANSFERROOTENTRY("../must-not-access-this"));
    testTransferRootsSetSingle(hTest, lstBase, lstBreakout, VERR_INVALID_PARAMETER);

    lstBreakout.clear();
    lstBase.append(TESTTRANSFERROOTENTRY("does-not-exist/file1.txt"));
    testTransferRootsSetSingle(hTest, lstBase, lstBreakout, VERR_INVALID_PARAMETER);

    lstBreakout.clear();
    lstBase.append(TESTTRANSFERROOTENTRY("my-transfer-1/../must-not-access-this"));
    testTransferRootsSetSingle(hTest, lstBase, lstBreakout, VERR_INVALID_PARAMETER);

    lstBreakout.clear();
    lstBase.append(TESTTRANSFERROOTENTRY("my-transfer-1/./../must-not-access-this"));
    testTransferRootsSetSingle(hTest, lstBase, lstBreakout, VERR_INVALID_PARAMETER);

    lstBreakout.clear();
    lstBase.append(TESTTRANSFERROOTENTRY("../does-not-exist"));
    testTransferRootsSetSingle(hTest, lstBase, lstBreakout, VERR_INVALID_PARAMETER);
}

static void testTransferObjOpen(RTTEST hTest)
{
    RTTestISub("Testing setting transfer object open");

    /* Define the (valid) transfer root set. */
    RTCList<TESTTRANSFERROOTENTRY> lstRoots;
    lstRoots.append(TESTTRANSFERROOTENTRY("my-transfer-1/file1.txt"));
    lstRoots.append(TESTTRANSFERROOTENTRY("my-transfer-1/dir1/file1.txt"));
    lstRoots.append(TESTTRANSFERROOTENTRY("my-transfer-1/dir1/sub1/file1.txt"));
    lstRoots.append(TESTTRANSFERROOTENTRY("my-transfer-1/dir2/file1.txt"));
    lstRoots.append(TESTTRANSFERROOTENTRY("my-transfer-1/dir2/sub1/file1.txt"));

    testTransferObjOpenSingle(hTest, lstRoots, "file1.txt", VINF_SUCCESS);
    testTransferObjOpenSingle(hTest, lstRoots, "does-not-exist.txt", VERR_PATH_NOT_FOUND);
    testTransferObjOpenSingle(hTest, lstRoots, "dir1/does-not-exist.txt", VERR_PATH_NOT_FOUND);
    testTransferObjOpenSingle(hTest, lstRoots, "../must-not-access-this.txt", VERR_INVALID_PARAMETER);
    testTransferObjOpenSingle(hTest, lstRoots, "dir1/../../must-not-access-this.txt", VERR_INVALID_PARAMETER);
}

int main(int argc, char *argv[])
{
    /*
     * Init the runtime, test and say hello.
     */
    const char *pcszExecName;
    NOREF(argc);
    pcszExecName = strrchr(argv[0], '/');
    pcszExecName = pcszExecName ? pcszExecName + 1 : argv[0];
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate(pcszExecName, &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    testTransferRootsSet(hTest);
    testTransferObjOpen(hTest);

    int rc = testRemoveTempDir(hTest);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(hTest);
}

