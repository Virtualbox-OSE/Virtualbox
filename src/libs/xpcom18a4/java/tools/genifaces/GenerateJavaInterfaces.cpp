/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Java XPCOM Bindings.
 *
 * The Initial Developer of the Original Code is
 * IBM Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2005
 * IBM Corporation. All Rights Reserved.
 *
 * Contributor(s):
 *   Javier Pedemonte (jhpedemonte@gmail.com)
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "nsXPCOM.h"
#include "nsString.h"
#include "nsILocalFile.h"
#include "nsIInterfaceInfoManager.h"
#include "xptinfo.h"
#include "nsCOMPtr.h"
#include "prmem.h"
#include "xptcall.h"
#ifdef VBOX
#include "nsFileStreams.h"
static nsresult
NS_NewLocalFileInputStream(nsIInputStream **aResult,
                           nsIFile         *aFile,
                           PRInt32          aIOFlags       = -1,
                           PRInt32          aPerm          = -1,
                           PRInt32          aBehaviorFlags = 0)
{
    nsFileInputStream* in = new nsFileInputStream();
    nsresult rv;

    rv = in->Init(aFile, aIOFlags, aPerm, aBehaviorFlags);
    if (NS_SUCCEEDED(rv))
        NS_ADDREF(*aResult = in);

    return rv;
}

inline nsresult
NS_NewLocalFileOutputStream(nsIOutputStream **aResult,
                            nsIFile          *aFile,
                            PRInt32           aIOFlags       = -1,
                            PRInt32           aPerm          = -1,
                            PRInt32           aBehaviorFlags = 0)
{
    nsFileOutputStream* in = new nsFileOutputStream();
    nsresult rv;

    rv = in->Init(aFile, aIOFlags, aPerm, aBehaviorFlags);
    if (NS_SUCCEEDED(rv))
        NS_ADDREF(*aResult = in);

    return rv;
}


#else
#include "nsNetUtil.h"
#endif
#include "nsHashSets.h"
#include "nsIWeakReference.h"
#include <stdio.h>

#ifdef WIN32
#define snprintf  _snprintf
#endif

#define WRITE_NOSCRIPT_METHODS


class TypeInfo
{
public:
  static nsresult GetParentInfo(nsIInterfaceInfo* aIInfo,
                                nsIInterfaceInfo** aParentInfo,
                                PRUint16* aParentMethodCount,
                                PRUint16* aParentConstCount)
  {
    nsCOMPtr<nsIInterfaceInfo> parent;
    nsresult rv = aIInfo->GetParent(getter_AddRefs(parent));
    NS_ENSURE_SUCCESS(rv, rv);

    if (!parent) {
      *aParentInfo = nsnull;
      *aParentMethodCount = 0;
      return NS_OK;
    }

    rv = parent->GetMethodCount(aParentMethodCount);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = parent->GetConstantCount(aParentConstCount);
    NS_ENSURE_SUCCESS(rv, rv);

    *aParentInfo = parent;
    NS_ADDREF(*aParentInfo);
    return rv;
  }

  static nsresult GetInterfaceName(nsIInterfaceInfo* aIInfo,
                                   PRUint16 aMethodIndex,
                                   const nsXPTParamInfo* aParamInfo,
                                   char** aResult)
  {
    static const char isupp_str[] = "nsISupports";

    nsIID* iid;
    nsresult rv = aIInfo->GetIIDForParam(aMethodIndex, aParamInfo, &iid);
    if (NS_FAILED(rv)) {
      // GetIIDForParam will sometimes fail to find an interface, particularly
      // if that interface is not defined in an IDL file.  In those cases, just
      // return |nsISupports|.
      //
      // For example, the |onStreamComplete| method for the interface
      // |nsIUnicharStreamLoaderObserver| takes a param of
      // |nsIUnicharInputStream|, which is defined in a simple header file, not
      // an IDL file.
      *aResult = (char*) nsMemory::Clone(isupp_str, sizeof(isupp_str));
      rv = (*aResult == nsnull) ? NS_ERROR_OUT_OF_MEMORY : NS_OK;

    } else {

      // In Javaconnect, we handle weak references internally; no need for the
      // |nsIWeakReference| interface.  So just return |nsISupports|.
      if (iid->Equals(NS_GET_IID(nsIWeakReference))) {
        *aResult = (char*) nsMemory::Clone(isupp_str, sizeof(isupp_str));
        rv = (*aResult == nsnull) ? NS_ERROR_OUT_OF_MEMORY : NS_OK;

      } else {

        // Some methods take parameters of non-scriptable interfaces.  But we
        // only output scriptable interfaces.  So if one of the param types is
        // a non-scriptable interface, output |nsISupports| instead of the
        // interface name.
        nsCOMPtr<nsIInterfaceInfo> info;
        nsCOMPtr<nsIInterfaceInfoManager> iim =
          getter_AddRefs(XPTI_GetInterfaceInfoManager());
        NS_ASSERTION(iim, "could not get interface info manager");
        rv = iim->GetInfoForIID(iid, getter_AddRefs(info));
        NS_ENSURE_SUCCESS(rv, rv);
        PRBool scriptable;
        if (NS_SUCCEEDED(rv)) {
          info->IsScriptable(&scriptable);
        }
        if (NS_FAILED(rv) || !scriptable) {
          *aResult = (char*) nsMemory::Clone(isupp_str, sizeof(isupp_str));
          rv = (*aResult == nsnull) ? NS_ERROR_OUT_OF_MEMORY : NS_OK;
        } else {

          // If is scriptable, get name for given IID
          rv = iim->GetNameForIID(iid, aResult);
        }
      }

      nsMemory::Free(iid);
    }

    return rv;
  }
};


static const char* kJavaKeywords[] = {
  "abstract", "default", "if"        , "private"     , "this"     ,
  "boolean" , "do"     , "implements", "protected"   , "throw"    ,
  "break"   , "double" , "import",     "public"      , "throws"   ,
  "byte"    , "else"   , "instanceof", "return"      , "transient",
  "case"    , "extends", "int"       , "short"       , "try"      ,
  "catch"   , "final"  , "interface" , "static"      , "void"     ,
  "char"    , "finally", "long"      , "strictfp"    , "volatile" ,
  "class"   , "float"  , "native"    , "super"       , "while"    ,
  "const"   , "for"    , "new"       , "switch"      ,
  "continue", "goto"   , "package"   , "synchronized",
  "assert"  ,  /* added in Java 1.4 */
  "enum"    ,  /* added in Java 5.0 */
  "clone"   ,  /* clone is a member function of java.lang.Object */
  "finalize"   /* finalize is a member function of java.lang.Object */
};

#ifdef WRITE_NOSCRIPT_METHODS
// SWT uses [noscript] methods of the following interfaces, so we need to
// output the [noscript] methods for these interfaces.
static const char* kNoscriptMethodIfaces[] = {
  "nsIBaseWindow", "nsIEmbeddingSiteWindow"
};
#endif


class Generate
{
  nsILocalFile*     mOutputDir;
  nsCStringHashSet  mIfaceTable;
  nsCStringHashSet  mJavaKeywords;
#ifdef WRITE_NOSCRIPT_METHODS
  nsCStringHashSet  mNoscriptMethodsTable;
#endif

public:
  Generate(nsILocalFile* aOutputDir)
    : mOutputDir(aOutputDir)
  {
    mIfaceTable.Init(100);

    PRUint32 size = NS_ARRAY_LENGTH(kJavaKeywords);
    mJavaKeywords.Init(size);
    for (PRUint32 i = 0; i < size; i++) {
      mJavaKeywords.Put(nsDependentCString(kJavaKeywords[i]));
    }

#ifdef WRITE_NOSCRIPT_METHODS
    size = NS_ARRAY_LENGTH(kNoscriptMethodIfaces);
    mNoscriptMethodsTable.Init(size);
    for (PRUint32 j = 0; j < size; j++) {
      mNoscriptMethodsTable.Put(nsDependentCString(kNoscriptMethodIfaces[j]));
    }
#endif
  }

  ~Generate()
  {
  }

  nsresult GenerateInterfaces()
  {
    nsresult rv;

    nsCOMPtr<nsIInterfaceInfoManager> iim =
      getter_AddRefs(XPTI_GetInterfaceInfoManager());
    NS_ASSERTION(iim, "could not get interface info manager");
    nsCOMPtr<nsIEnumerator> etor;
    rv = iim->EnumerateInterfaces(getter_AddRefs(etor));
    NS_ENSURE_SUCCESS(rv, rv);

    // loop over interfaces
    etor->First();
    do {
      // get current interface
      nsCOMPtr<nsISupports> item;
      rv = etor->CurrentItem(getter_AddRefs(item));
      NS_ENSURE_SUCCESS(rv, rv);

      nsCOMPtr<nsIInterfaceInfo> iface(do_QueryInterface(item));
      if (!iface)
        break;

      // we only care about scriptable interfaces, so skip over those
      // that aren't
      PRBool scriptable;
      iface->IsScriptable(&scriptable);
      if (!scriptable) {
        // XXX SWT uses non-scriptable interface 'nsIAppShell' (bug 270892), so
        // include that one.
        const char* iface_name;
        iface->GetNameShared(&iface_name);
        if (strcmp("nsIAppShell", iface_name) != 0)
          continue;
      }

      rv = WriteOneInterface(iface);
      NS_ENSURE_SUCCESS(rv, rv);

    } while (NS_SUCCEEDED(etor->Next()));

    return NS_OK;
  }

  nsresult WriteOneInterface(nsIInterfaceInfo* aIInfo)
  {
    nsresult rv;

    // write each interface only once
    const char* iface_name;
    aIInfo->GetNameShared(&iface_name);
    if (mIfaceTable.Contains(nsDependentCString(iface_name)))
      return NS_OK;

    // write any parent interface
    nsCOMPtr<nsIInterfaceInfo> parentInfo;
    PRUint16 parentMethodCount, parentConstCount;
    rv = TypeInfo::GetParentInfo(aIInfo, getter_AddRefs(parentInfo),
                                 &parentMethodCount, &parentConstCount);
    NS_ENSURE_SUCCESS(rv, rv);
    if (parentInfo)
      WriteOneInterface(parentInfo);

    mIfaceTable.Put(nsDependentCString(iface_name));

    // create file for interface
    nsCOMPtr<nsIOutputStream> out;
    rv = OpenIfaceFileStream(iface_name, getter_AddRefs(out));
    NS_ENSURE_SUCCESS(rv, rv);

    // write contents to file
    rv = WriteHeader(out, iface_name);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = WriteInterfaceStart(out, aIInfo, parentInfo);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = WriteIID(out, aIInfo);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = WriteConstants(out, aIInfo, parentConstCount);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = WriteMethods(out, aIInfo, parentMethodCount);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = WriteInterfaceEnd(out);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = CloseIfaceFileStream(out);

    return rv;
  }

  nsresult OpenIfaceFileStream(const char* aIfaceName,
                               nsIOutputStream** aResult)
  {
    nsresult rv;

    // create interface file in output dir
    nsCOMPtr<nsIFile> iface_file;
    rv = mOutputDir->Clone(getter_AddRefs(iface_file));
    NS_ENSURE_SUCCESS(rv, rv);
    nsAutoString filename;
    filename.AppendASCII(aIfaceName);
    filename.AppendLiteral(".java");
    rv = iface_file->Append(filename);
    NS_ENSURE_SUCCESS(rv, rv);

    // create interface file
    PRBool exists;
    iface_file->Exists(&exists);
    if (exists)
      iface_file->Remove(PR_FALSE);
    rv = iface_file->Create(nsIFile::NORMAL_FILE_TYPE, 0644);
    NS_ENSURE_SUCCESS(rv, rv);

    // create output stream for writing to file
    nsCOMPtr<nsIOutputStream> out;
    rv = NS_NewLocalFileOutputStream(getter_AddRefs(out), iface_file);
    NS_ENSURE_SUCCESS(rv, rv);

    *aResult = out;
    NS_ADDREF(*aResult);
    return NS_OK;
  }

  nsresult CloseIfaceFileStream(nsIOutputStream* out)
  {
    return out->Close();
  }

  nsresult WriteHeader(nsIOutputStream* out, const char* aIfaceName)
  {
    static const char kHeader1[] =
      "/**\n"
      " * NOTE: THIS IS A GENERATED FILE. PLEASE CONSULT THE ORIGINAL IDL FILE\n"
      " * FOR THE FULL DOCUMENTION AND LICENSE.\n"
      " *\n"
      " * @see <a href=\"http://lxr.mozilla.org/mozilla/search?string=";
    static const char kHeader2[]= "\">\n **/\n\n";
    static const char kPackage[] = "package org.mozilla.interfaces;\n\n";

    PRUint32 count;
    nsresult rv = out->Write(kHeader1, sizeof(kHeader1) - 1, &count);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCAutoString searchTerm;
    searchTerm.AppendLiteral("interface+");
    searchTerm.AppendASCII(aIfaceName);
    // LXR limits to 29 chars
    rv = out->Write(searchTerm.get(), PR_MIN(29, searchTerm.Length()), &count);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = out->Write(kHeader2, sizeof(kHeader2) - 1, &count);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = out->Write(kPackage, sizeof(kPackage) - 1, &count);
    return rv;
  }

  nsresult WriteInterfaceStart(nsIOutputStream* out, nsIInterfaceInfo* aIInfo,
                              nsIInterfaceInfo* aParentInfo)
  {
    static const char kIfaceDecl1[] = "public interface ";
    static const char kParentDecl[] = " extends ";
    static const char kIfaceDecl2[] = "\n{\n";

    const char* iface_name;
    aIInfo->GetNameShared(&iface_name);
    PRUint32 count;
    nsresult rv = out->Write(kIfaceDecl1, sizeof(kIfaceDecl1) - 1, &count);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = out->Write(iface_name, strlen(iface_name), &count);
    NS_ENSURE_SUCCESS(rv, rv);

    if (aParentInfo) {
      const char* parent_name;
      aParentInfo->GetNameShared(&parent_name);
      rv = out->Write(kParentDecl, sizeof(kParentDecl) - 1, &count);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = out->Write(parent_name, strlen(parent_name), &count);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    rv = out->Write(kIfaceDecl2, sizeof(kIfaceDecl2) - 1, &count);
    return rv;
  }

  nsresult WriteInterfaceEnd(nsIOutputStream* out)
  {
    PRUint32 count;
    return out->Write("}\n", 2, &count);
  }

  nsresult WriteIID(nsIOutputStream* out, nsIInterfaceInfo* aIInfo)
  {
    static const char kIIDDecl1[] = "  public static final String ";
    static const char kIIDDecl2[] = " =\n    \"";
    static const char kIIDDecl3[] = "\";\n\n";

    nsIID* iid = nsnull;
    aIInfo->GetInterfaceIID(&iid);
    if (!iid)
      return NS_ERROR_OUT_OF_MEMORY;

    // create iid field name
    nsCAutoString iid_name;
    const char* iface_name;
    aIInfo->GetNameShared(&iface_name);
    if (strncmp("ns", iface_name, 2) == 0) {
      iid_name.AppendLiteral("NS_");
      iid_name.Append(iface_name + 2);
    } else {
      iid_name.Append(iface_name);
    }
    iid_name.AppendLiteral("_IID");
    ToUpperCase(iid_name);

    // get iid string
    char* iid_str = iid->ToString();
    if (!iid_str)
      return NS_ERROR_OUT_OF_MEMORY;

    PRUint32 count;
    nsresult rv = out->Write(kIIDDecl1, sizeof(kIIDDecl1) - 1, &count);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = out->Write(iid_name.get(), iid_name.Length(), &count);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = out->Write(kIIDDecl2, sizeof(kIIDDecl2) - 1, &count);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = out->Write(iid_str, strlen(iid_str), &count);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = out->Write(kIIDDecl3, sizeof(kIIDDecl3) - 1, &count);
    NS_ENSURE_SUCCESS(rv, rv);

    // cleanup
    PR_Free(iid_str);
    nsMemory::Free(iid);
    return NS_OK;
  }

  nsresult WriteConstants(nsIOutputStream* out, nsIInterfaceInfo* aIInfo,
                          PRUint16 aParentConstCount)
  {
    static const char kConstDecl1[] = "  public static final ";
    static const char kConstDecl2[] = " = ";
    static const char kConstDecl3[] = ";\n\n";

    PRUint16 constCount;
    nsresult rv = aIInfo->GetConstantCount(&constCount);
    NS_ENSURE_SUCCESS(rv, rv);

    for (PRUint16 i = aParentConstCount; i < constCount; i++) {
      const nsXPTConstant* constInfo;
      rv = aIInfo->GetConstant(i, &constInfo);
      NS_ENSURE_SUCCESS(rv, rv);

      PRUint32 count;
      rv = out->Write(kConstDecl1, sizeof(kConstDecl1) - 1, &count);
      NS_ENSURE_SUCCESS(rv, rv);
      const nsXPTType &type = constInfo->GetType();
      rv = WriteType(out, &type, aIInfo, nsnull, nsnull);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = out->Write(" ", 1, &count);
      NS_ENSURE_SUCCESS(rv, rv);
      const char* name = constInfo->GetName();
      rv = out->Write(name, strlen(name), &count);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = out->Write(kConstDecl2, sizeof(kConstDecl2) - 1, &count);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = WriteConstantValue(out, &type, constInfo->GetValue());
      NS_ENSURE_SUCCESS(rv, rv);
      rv = out->Write(kConstDecl3, sizeof(kConstDecl3) - 1, &count);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    return NS_OK;
  }

  nsresult WriteConstantValue(nsIOutputStream* out, const nsXPTType* aType,
                              const nsXPTCMiniVariant* aValue)
  {
    char buf[32];
    switch (aType->TagPart()) {
      case nsXPTType::T_I8:
        snprintf(buf, sizeof(buf), "%d", aValue->val.i8);
        break;

      case nsXPTType::T_U8:
        snprintf(buf, sizeof(buf), "%u", aValue->val.u8);
        break;

      case nsXPTType::T_I16:
        snprintf(buf, sizeof(buf), "%d", aValue->val.i16);
        break;

      case nsXPTType::T_U16:
        snprintf(buf, sizeof(buf), "%u", aValue->val.u16);
        break;

      case nsXPTType::T_I32:
        snprintf(buf, sizeof(buf), "%d", aValue->val.i32);
        break;

      case nsXPTType::T_U32:
        snprintf(buf, sizeof(buf), "%uL", aValue->val.u32);
        break;

      case nsXPTType::T_I64:
        snprintf(buf, sizeof(buf), "%lldL", aValue->val.i64);
        break;

      case nsXPTType::T_U64:
        snprintf(buf, sizeof(buf), "%lluL", aValue->val.u64);
        break;

      case nsXPTType::T_FLOAT:
        snprintf(buf, sizeof(buf), "%f", aValue->val.f);
        break;

      case nsXPTType::T_DOUBLE:
        snprintf(buf, sizeof(buf), "%f", aValue->val.d);
        break;

      case nsXPTType::T_BOOL:
        if (aValue->val.b)
          sprintf(buf, "true");
        else
          sprintf(buf, "false");
        break;

      case nsXPTType::T_CHAR:
        snprintf(buf, sizeof(buf), "%c", aValue->val.c);
        break;

      case nsXPTType::T_WCHAR:
        snprintf(buf, sizeof(buf), "%c", aValue->val.wc);
        break;

      default:
        NS_WARNING("unexpected constant type");
        return NS_ERROR_UNEXPECTED;
    }

    PRUint32 count;
    return out->Write(buf, strlen(buf), &count);
  }

  nsresult WriteMethods(nsIOutputStream* out, nsIInterfaceInfo* aIInfo,
                        PRUint16 aParentMethodCount)
  {
    PRUint16 methodCount;
    nsresult rv = aIInfo->GetMethodCount(&methodCount);
    NS_ENSURE_SUCCESS(rv, rv);

#ifdef DEBUG_bird /* attributes first, then method. For making diffing easier. */
    for (int pass = 0; pass < 2; pass++)
#endif
    for (PRUint16 i = aParentMethodCount; i < methodCount; i++) {
      const nsXPTMethodInfo* methodInfo;
      rv = aIInfo->GetMethodInfo(i, &methodInfo);
      NS_ENSURE_SUCCESS(rv, rv);

#ifdef WRITE_NOSCRIPT_METHODS
      // XXX
      // SWT makes use of [noscript] methods in some classes, so output them
      // for those classes.

      // skip [notxpcom] methods
      if (methodInfo->IsNotXPCOM())
        continue;

      // skip most hidden ([noscript]) methods
      if (methodInfo->IsHidden()) {
        const char* iface_name;
        aIInfo->GetNameShared(&iface_name);
        if (!mNoscriptMethodsTable.Contains(nsDependentCString(iface_name)))
          continue;
      }
#else
      // skip hidden ([noscript]) or [notxpcom] methods
      if (methodInfo->IsHidden() || methodInfo->IsNotXPCOM())
        continue;
#endif
#ifdef DEBUG_bird /* attributes first, then method. For making diffing easier. */
      if ((methodInfo->IsSetter() || methodInfo->IsGetter()) == pass)
          continue;
#endif

      rv = WriteOneMethod(out, aIInfo, methodInfo, i);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    return NS_OK;
  }

  nsresult WriteOneMethod(nsIOutputStream* out, nsIInterfaceInfo* aIInfo,
                          const nsXPTMethodInfo* aMethodInfo,
                          PRUint16 aMethodIndex)
  {
    static const char kMethodDecl1[] = "  public ";
    static const char kVoidReturn[] = "void";
    static const char kParamSeparator[] = ", ";
    static const char kMethodDecl2[] = ");\n\n";

    PRUint32 count;
    nsresult rv = out->Write(kMethodDecl1, sizeof(kMethodDecl1) - 1, &count);
    NS_ENSURE_SUCCESS(rv, rv);

    // write return type
    PRUint8 paramCount = aMethodInfo->GetParamCount();
    const nsXPTParamInfo* resultInfo = nsnull;
    for (PRUint8 i = 0; i < paramCount; i++) {
      const nsXPTParamInfo &paramInfo = aMethodInfo->GetParam(i);
      if (paramInfo.IsRetval()) {
        resultInfo = &paramInfo;
        break;
      }
    }
    if (resultInfo) {
      rv = WriteParam(out, aIInfo, aMethodIndex, resultInfo, 0);
    } else {
      rv = out->Write(kVoidReturn, sizeof(kVoidReturn) - 1, &count);
    }
    NS_ENSURE_SUCCESS(rv, rv);

    // write method name string
    nsCAutoString method_name;
    const char* name = aMethodInfo->GetName();
    if (aMethodInfo->IsGetter() || aMethodInfo->IsSetter()) {
      if (aMethodInfo->IsGetter())
        method_name.AppendLiteral("get");
      else
        method_name.AppendLiteral("set");
      method_name.Append(toupper(name[0]));
      method_name.AppendASCII(name + 1);
    } else {
      method_name.Append(tolower(name[0]));
      method_name.AppendASCII(name + 1);
      // don't use Java keywords as method names
      if (mJavaKeywords.Contains(method_name))
        method_name.Append('_');
    }
    rv = out->Write(" ", 1, &count);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = out->Write(method_name.get(), method_name.Length(), &count);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = out->Write("(", 1, &count);
    NS_ENSURE_SUCCESS(rv, rv);

    // write parameters
    for (PRUint8 j = 0; j < paramCount; j++) {
      const nsXPTParamInfo &paramInfo = aMethodInfo->GetParam(j);
      if (paramInfo.IsRetval())
        continue;

      if (j != 0) {
        rv = out->Write(kParamSeparator, sizeof(kParamSeparator) - 1, &count);
        NS_ENSURE_SUCCESS(rv, rv);
      }

      rv = WriteParam(out, aIInfo, aMethodIndex, &paramInfo, j + 1);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    rv = out->Write(kMethodDecl2, sizeof(kMethodDecl2) - 1, &count);
    return rv;
  }

  nsresult WriteParam(nsIOutputStream* out, nsIInterfaceInfo* aIInfo,
                      PRUint16 aMethodIndex, const nsXPTParamInfo* aParamInfo,
                      PRUint8 aIndex)
  {
    const nsXPTType &type = aParamInfo->GetType();
    nsresult rv = WriteType(out, &type, aIInfo, aMethodIndex, aParamInfo);
    NS_ENSURE_SUCCESS(rv, rv);

    // if parameter is 'out' or 'inout', make it a Java array
    PRUint32 count;
    if (aParamInfo->IsOut() && !aParamInfo->IsRetval()) {
      rv = out->Write("[]", 2, &count);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // write name for parameter (but not for 'retval' param)
    if (aIndex) {
      char buf[10];
      snprintf(buf, sizeof(buf), " arg%d", aIndex);
      rv = out->Write(buf, strlen(buf), &count);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    return NS_OK;
  }

  /**
   * Write out the Java type for the given XPIDL type.
   *
   * NOTE: Java doesn't support unsigned types.  So for any unsigned XPIDL type,
   * we move up to the next largest Java type.  This way we ensure that we don't
   * lose any info.
   */
  nsresult WriteType(nsIOutputStream* out, const nsXPTType* aType,
                     nsIInterfaceInfo* aIInfo, PRUint16 aMethodIndex,
                     const nsXPTParamInfo* aParamInfo)
  {
    nsresult rv;
    PRUint32 count;
    switch (aType->TagPart()) {
      case nsXPTType::T_I8:
        rv = out->Write("byte", 4, &count);
        break;

      case nsXPTType::T_I16:
      case nsXPTType::T_U8:
        rv = out->Write("short", 5, &count);
        break;

      case nsXPTType::T_I32:
      case nsXPTType::T_U16:
        rv = out->Write("int", 3, &count);
        break;

      case nsXPTType::T_I64:
      case nsXPTType::T_U32:
        rv = out->Write("long", 4, &count);
        break;

      case nsXPTType::T_FLOAT:
        rv = out->Write("float", 5, &count);
        break;

      // XXX how should we handle 64-bit values?
      case nsXPTType::T_U64:
      case nsXPTType::T_DOUBLE:
        rv = out->Write("double", 6, &count);
        break;

      case nsXPTType::T_BOOL:
        rv = out->Write("boolean", 7, &count);
        break;

      case nsXPTType::T_CHAR:
      case nsXPTType::T_WCHAR:
        rv = out->Write("char", 4, &count);
        break;

      case nsXPTType::T_CHAR_STR:
      case nsXPTType::T_WCHAR_STR:
      case nsXPTType::T_IID:
      case nsXPTType::T_ASTRING:
      case nsXPTType::T_DOMSTRING:
      case nsXPTType::T_UTF8STRING:
      case nsXPTType::T_CSTRING:
      case nsXPTType::T_PSTRING_SIZE_IS:
      case nsXPTType::T_PWSTRING_SIZE_IS:
        rv = out->Write("String", 6, &count);
        break;

      case nsXPTType::T_INTERFACE:
      {
        char* iface_name = nsnull;
        rv = TypeInfo::GetInterfaceName(aIInfo, aMethodIndex, aParamInfo,
                                        &iface_name);
        if (NS_FAILED(rv) || !iface_name) {
          rv = NS_ERROR_FAILURE;
          break;
        }

        rv = out->Write(iface_name, strlen(iface_name), &count);
        nsMemory::Free(iface_name);
        break;
      }

      case nsXPTType::T_INTERFACE_IS:
        rv = out->Write("nsISupports", 11, &count);
        break;

      case nsXPTType::T_VOID:
        rv = out->Write("int", 3, &count);
        break;

      case nsXPTType::T_ARRAY:
      {
        // get array type
        nsXPTType xpttype;
        rv = aIInfo->GetTypeForParam(aMethodIndex, aParamInfo, 1, &xpttype);
        if (NS_FAILED(rv))
          break;

        rv = WriteType(out, &xpttype, aIInfo, aMethodIndex, aParamInfo);
        if (NS_FAILED(rv))
          break;

        rv = out->Write("[]", 2, &count);
        break;
      }

      default:
        fprintf(stderr, "WARNING: unexpected parameter type %d\n",
                aType->TagPart());
        return NS_ERROR_UNEXPECTED;
    }

    return rv;
  }
};

void PrintUsage(char** argv)
{
  static const char usage_str[] =
      "Usage: %s -d path\n"
      "         -d output directory for Java interface files\n";
  fprintf(stderr, usage_str, argv[0]);
}
#ifdef VBOX
#include <VBox/com/com.h>
using namespace com;

#include <iprt/initterm.h>
#include <iprt/string.h>
#include <iprt/alloca.h>
#include <iprt/stream.h>
#endif

int main(int argc, char** argv)
{
  nsresult rv = NS_OK;
  nsCOMPtr<nsILocalFile> output_dir;

#ifdef VBOX
#if defined(VBOX_PATH_APP_PRIVATE_ARCH) && defined(VBOX_PATH_SHARED_LIBS)
  rv = RTR3InitExe(argc, &argv, 0);
#else
  const char *pszHome = getenv("VBOX_PROGRAM_PATH");
  if (pszHome) {
    size_t cchHome = strlen(pszHome);
    char *pszExePath = (char *)alloca(cchHome + 32);
    memcpy(pszExePath, pszHome, cchHome);
    memcpy(pszExePath + cchHome, "/pythonfake", sizeof("/pythonfake"));
    rc = RTR3InitEx(RTR3INIT_VER_CUR, 0, argc, &argv, pszExePath);
  } else {
    rv = RTR3InitExe(argc, &argv, 0);
  }
#endif
#endif

  // handle command line arguments
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] != '-') {
      rv = NS_ERROR_FAILURE;
      break;
    }

    switch (argv[i][1]) {
      case 'd': {
        if (i + 1 == argc) {
          fprintf(stderr, "ERROR: missing output directory after -d\n");
          rv = NS_ERROR_FAILURE;
          break;
        }

        // see if given path exists
        rv = NS_NewNativeLocalFile(nsDependentCString(argv[++i]), PR_TRUE,
                                   getter_AddRefs(output_dir));
        PRBool val;
        if (NS_FAILED(rv) || NS_FAILED(output_dir->Exists(&val)) || !val ||
            NS_FAILED(output_dir->IsDirectory(&val)) || !val)
        {
          fprintf(stderr,
                  "ERROR: output directory doesn't exist / isn't a directory\n");
          rv = NS_ERROR_FAILURE;
          break;
        }

        break;
      }

      default: {
        fprintf(stderr, "ERROR: unknown option %s\n", argv[i]);
        rv = NS_ERROR_FAILURE;
        break;
      }
    }
  }

  if (NS_FAILED(rv)) {
    PrintUsage(argv);
    return 1;
  }

#ifdef VBOX
  rv = com::Initialize();
#else
  rv = NS_InitXPCOM2(nsnull, nsnull, nsnull);
#endif
  NS_ENSURE_SUCCESS(rv, rv);

  Generate gen(output_dir);
  rv = gen.GenerateInterfaces();

#ifdef VBOX
  // very short-living XPCOM processes trigger problem on shutdown - ignoring it not a big deal anyway
  //com::Shutdown();
#else
  NS_ShutdownXPCOM(nsnull);
#endif
  return rv;
}
