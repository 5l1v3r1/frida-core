#define COBJMACROS 1

#include "zed-core.h"

#include "windows-icon-helpers.h"

#include <psapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <unknwn.h>

#define PARSE_STRING_MAX_LENGTH (40 + 1)

ZedImageData *
_zed_service_windows_host_session_provider_extract_icon (GError ** error)
{
  ZedImageData * result = NULL;
  OLECHAR my_computer_parse_string[PARSE_STRING_MAX_LENGTH];
  IShellFolder * desktop_folder = NULL;
  IEnumIDList * children = NULL;
  ITEMIDLIST * child;

  wcscpy_s (my_computer_parse_string, PARSE_STRING_MAX_LENGTH, L"::");
  StringFromGUID2 (&CLSID_MyComputer, my_computer_parse_string + 2, PARSE_STRING_MAX_LENGTH - 2);

  if (SHGetDesktopFolder (&desktop_folder) != S_OK)
    goto beach;

  if (IShellFolder_EnumObjects (desktop_folder, NULL, SHCONTF_FOLDERS, &children) != S_OK)
    goto beach;

  while (result == NULL && IEnumIDList_Next (children, 1, &child, NULL) == S_OK)
  {
    STRRET display_name_value;
    WCHAR display_name[MAX_PATH];
    SHFILEINFOW file_info = { 0, };

    if (IShellFolder_GetDisplayNameOf (desktop_folder, child, SHGDN_FORPARSING, &display_name_value) != S_OK)
      goto next_child;
    StrRetToBufW (&display_name_value, child, display_name, MAX_PATH);

    if (_wcsicmp (display_name, my_computer_parse_string) != 0)
      goto next_child;

    if (SHGetFileInfoW ((LPCWSTR) child, 0, &file_info, sizeof (file_info), SHGFI_PIDL | SHGFI_ICON | SHGFI_SMALLICON | SHGFI_ADDOVERLAYS) == 0)
      goto next_child;

    result = _zed_image_data_from_native_icon_handle (file_info.hIcon, ZED_ICON_SMALL);

    DestroyIcon (file_info.hIcon);

next_child:
    CoTaskMemFree (child);
  }

beach:
  if (children != NULL)
    IUnknown_Release (children);
  if (desktop_folder != NULL)
    IUnknown_Release (desktop_folder);

  return result;
}

ZedHostProcessInfo *
zed_service_windows_process_backend_enumerate_processes_sync (
    int * result_length1)
{
  GArray * processes;
  DWORD * pids = NULL;
  DWORD size = 64 * sizeof (DWORD);
  DWORD bytes_returned;
  guint i;

  processes = g_array_new (FALSE, FALSE, sizeof (ZedHostProcessInfo));

  do
  {
    size *= 2;
    pids = (DWORD *) g_realloc (pids, size);
    if (!EnumProcesses (pids, size, &bytes_returned))
      bytes_returned = 0;
  }
  while (bytes_returned == size);

  for (i = 0; i != bytes_returned / sizeof (DWORD); i++)
  {
    HANDLE handle;

    handle = OpenProcess (PROCESS_QUERY_INFORMATION, FALSE, pids[i]);
    if (handle != NULL)
    {
      WCHAR name_utf16[MAX_PATH];
      DWORD name_length = MAX_PATH;

      if (QueryFullProcessImageNameW (handle, 0, name_utf16, &name_length))
      {
        gchar * name, * tmp;
        ZedHostProcessInfo * process_info;
        ZedImageData * small_icon, * large_icon;

        name = g_utf16_to_utf8 ((gunichar2 *) name_utf16, -1, NULL, NULL, NULL);
        tmp = g_path_get_basename (name);
        g_free (name);
        name = tmp;

        small_icon = _zed_image_data_from_process_or_file (pids[i], name_utf16, ZED_ICON_SMALL);
        large_icon = _zed_image_data_from_process_or_file (pids[i], name_utf16, ZED_ICON_LARGE);

        g_array_set_size (processes, processes->len + 1);
        process_info = &g_array_index (processes, ZedHostProcessInfo, processes->len - 1);
        zed_host_process_info_init (process_info, pids[i], name, small_icon, large_icon);

        zed_image_data_free (large_icon);
        zed_image_data_free (small_icon);

        g_free (name);
      }

      CloseHandle (handle);
    }
  }

  g_free (pids);

  *result_length1 = processes->len;
  return (ZedHostProcessInfo *) g_array_free (processes, FALSE);
}