#include <stdio.h>
#include "snapconfig.h"
#include "util/fileutil.h"
#include "coordsys/coordsys.h"
#include "snap/filenames.h"
#include "util/versioninfo.h"
#include "wx_includes.hpp"
#include "snap_scriptenv.hpp"
#include "fstream"

using namespace std;

#define SNAPSCRIPT_DIR "snapscript"

//extern "C"
//{
#include "coordsys/coordsys.h"
//}

// Events generated by the script environment

DEFINE_EVENT_TYPE(wxEVT_SNAP_JOBUPDATED);
DEFINE_EVENT_TYPE(wxEVT_SNAP_CLEARLOG);

// Maximum replacements by replacement funtion, to avoid indefinite loops with replacing 0 length string

const int maxReplace = 10000;

BEGIN_EVENT_TABLE( SnapMgrScriptEnv, wxEvtHandler )
END_EVENT_TABLE()

SnapMgrScriptEnv::SnapMgrScriptEnv( wxFrame *frameWindow )
    : frameWindow( frameWindow )
{
    job = 0;
	coordsyslist="";
	heightreflist="";
    script = new Script( *this );
    SetupConfiguration();
    frameWindow->PushEventHandler(this);
}

SnapMgrScriptEnv::~SnapMgrScriptEnv()
{
    frameWindow->PopEventHandler();
    UnloadJob( false );
    uninstall_crdsys_lists();
    for( size_t i = 0; i < tmpFiles.Count(); i++ )
    {
        ::wxRemoveFile( tmpFiles[i] );
    }
    delete script;
}

void SnapMgrScriptEnv::SetupConfiguration()
{
    // Add the image path to the path variable ...
    // Mainly for the shell command ..

    AddSnapDirToPath();

	scriptPath=wxString(_T(system_config_dir()));
	scriptPath.Append(_T(PATH_SEPARATOR));
	scriptPath.Append(SNAPSCRIPT_DIR);

	userScriptPath=wxString(_T(user_config_dir()));
	userScriptPath.Append(_T(PATH_SEPARATOR));
	userScriptPath.Append(SNAPSCRIPT_DIR);

	const char *cfgfile=find_config_file(SNAPSCRIPT_DIR,"snap_manager.cfg",0);
    if( cfgfile )
    {
        script->ExecuteScript( cfgfile );
    }
}

wxString &SnapMgrScriptEnv::GetCoordSysList()
{
	reset_config_dirs();
    install_default_crdsys_file();
    coordsyslist.Empty();
    for( int i = 0; i < coordsys_list_count(); i++ )
    {
        coordsyslist.append(_T("\n"));
        coordsyslist.append(_T(coordsys_list_code(i)));
        coordsyslist.append(_T("\n"));
        coordsyslist.append(_T(coordsys_list_desc(i)));
    }
	return coordsyslist;
}

wxString &SnapMgrScriptEnv::GetHeightRefList()
{
	reset_config_dirs();
    install_default_crdsys_file();
    heightreflist.Empty();
    for( int i = 0; i < vdatum_list_count(); i++ )
    {
        heightreflist.append(_T("\n"));
        heightreflist.append(_T(vdatum_list_code(i)));
        heightreflist.append(_T("\n"));
        heightreflist.append(_T(vdatum_list_desc(i)));
    }
	return heightreflist;
}

bool SnapMgrScriptEnv::LoadJob( const wxString &jobFile )
{
    if( ! UnloadJob( true ) ) return false;

    {
        wxCommandEvent evt( wxEVT_SNAP_JOBUPDATED );
        evt.SetInt( SNAP_JOBLOADING );
        frameWindow->ProcessEvent(evt);
    }

    wxFileName jobfilename( jobFile );
    if( ! jobfilename.FileExists())
    {
        wxString error(_T("Command file "));
        error << jobFile;
        error << " does not exists";
        ReportError(error);
        return false;
    }
    jobfilename.MakeAbsolute();
    wxSetWorkingDirectory( jobfilename.GetPath() );

    job = new SnapJob(jobFile);

    {
        wxCommandEvent evt( wxEVT_SNAP_JOBUPDATED );
        evt.SetInt( SNAP_JOBUPDATED_NEWJOB );
        frameWindow->ProcessEvent(evt);
    }

    script->EnableMenuItems();

    return true;
}

bool SnapMgrScriptEnv::UnloadJob( bool canVeto )
{
    if( ! job ) return true;
    if( canVeto && ! job->IsSaved() )
    {
        wxString message(_T("Job "));
        message.Append( job->GetFilename() );
        message.Append( " has not been saved. Save now?");
        int result = wxMessageBox( message, _T("Save job?"), wxYES_NO | wxCANCEL | wxICON_QUESTION );
        if( result == wxCANCEL ) return false;
        if( result == wxYES ) job->Save();
    }
    delete job;
    job = 0;
    wxCommandEvent evt( wxEVT_SNAP_JOBUPDATED );
    evt.SetInt( SNAP_JOBUPDATED_NEWJOB );
    frameWindow->ProcessEvent(evt);
    script->EnableMenuItems();
    return true;
}

bool SnapMgrScriptEnv::UpdateJob()
{
    bool updated = false;
    if( job ) updated = job->Update();
    if( updated )
    {
        wxCommandEvent evt( wxEVT_SNAP_JOBUPDATED );
        evt.SetInt( 0 );
        frameWindow->ProcessEvent(evt);
    }
    return updated;
}

void SnapMgrScriptEnv::InsertPath( const wxString &path, const wxString &envvar )
{
	if( path.IsEmpty()) return;
	// Ensure path is using correct delimiter
	wxString psep=PATH_SEPARATOR;
	wxString psep2=PATH_SEPARATOR2;
	wxString envsep=PATHENV_SEP;
	wxString pathval=path;
	pathval.Replace(psep2,psep);

	// Remove the

	wxString envval;
    ::wxGetEnv( envvar, &envval )	;
	if( envval.IsEmpty() )
	{
		envval = pathval;
	}
	else
	{
		wxArrayString paths=::wxStringTokenize(envval,envsep);
		envval=pathval;
		for( size_t i = 0; i < paths.Count(); i++ )
		{
			if( paths[i] != pathval )
			{
				envval.Append(_T(PATHENV_SEP));
				envval.Append(paths[i]);
			}
		}
	}
	::wxSetEnv(envvar,envval);
#ifdef __WINDOWS__
    // For windows need to modify environment as well
    // http://wx-users.wxwidgets.narkive.com/P0A4LE9k/wxsetenv-and-putenv
    //   Under Windows the former uses the Win32 function while the latter modifies
    //   the CRT env var block. So if you use Win32 GetEnvironmentVariable(), the
    //   latter wouldn't have any effect. While the former doesn't have any effect
    //   if you use CRT getenv() which refers to existing env block.
    wxString setenv = envvar + _T("=") + envval;
    _putenv( setenv.c_str() );
#endif
}

void SnapMgrScriptEnv::AddSnapDirToPath()
{
	InsertPath( image_dir());
}

bool SnapMgrScriptEnv::RemoveDirectory(const wxString &dirpath)
{
    bool dir_empty = false;
    {
        wxDir dir(dirpath);
        if ( !dir.IsOpened() )
        {
            return false;
        }
        wxString file;

        bool cont = dir.GetFirst(&file);
        while ( cont )
        {
            file = dirpath + wxFileName::GetPathSeparator()+ file;
            if(wxFileName::FileExists(file)) wxRemoveFile(file);
            if( wxFileName::DirExists(file)) RemoveDirectory(file);
            cont = dir.GetNext(&file);
        }
        if(!(dir.HasFiles() || dir.HasSubDirs())) dir_empty = true;
    }
    if(dir_empty)
    {
        return wxRmdir(dirpath);
    }
    return false;
}

void SnapMgrScriptEnv::ReportError( const wxString &error )
{
    wxMessageBox( error,_T("SNAP - error"), wxOK | wxICON_ERROR );
}

wxMenuItem *SnapMgrScriptEnv::GetMenuItemByLabel( wxMenu *menu, const wxString &label, bool wantSubMenu )
{
    for( size_t i = 0; i < menu->GetMenuItemCount(); i++ )
    {
        wxMenuItem *mi=menu->FindItemByPosition(i);
        if( wantSubMenu && ! mi->GetSubMenu()) continue;
        if( ! wantSubMenu && mi->GetSubMenu()) continue;
        if( mi->GetItemLabel()==label )
        {
            return mi;
        }
    }
    return 0;
}

size_t SnapMgrScriptEnv::GetMenuInsertPosition( wxMenu *menu )
{
    size_t insertPos;
    for( insertPos=0; insertPos < menu->GetMenuItemCount(); insertPos++ )
    {
        if( menu->FindItemByPosition(insertPos)->GetId() == wxID_SEPARATOR ) break;
    }
    return insertPos;
}

wxMenuItem *SnapMgrScriptEnv::GetMenuItem( const wxString &name, wxMenu **parent, bool createParents )
{
    // Set up the file menu ..

    wxMenuBar *menuBar = frameWindow->GetMenuBar();
    if( parent ) (*parent)=0;
    if( ! menuBar ) return 0;

    wxString delimiter=_T("|");
    bool wantMenu = name.EndsWith(delimiter.c_str());
    wxString menuName=name;
    if( wantMenu ) { menuName=name.BeforeLast('|'); }

    // Must have at least one sub menu ... put into a "&Scripts" menu if there isn't one

    wxStringTokenizer menuParts(menuName,_T("|"));

    if( menuParts.CountTokens() < 1 ) return 0;
    if( menuParts.CountTokens() > 1 )
    {
        menuName = menuParts.GetNextToken();
    }
    else
    {
        menuName = wxString(_T("&Scripts"));
    }

    // Find the menu, or create it if it doesn't exist...

    int menuId = menuBar->FindMenu( menuName );
    wxMenu *menu;
    if( menuId == wxNOT_FOUND )
    {
        if( ! createParents ) return 0;
        menu = new wxMenu;
        menuBar->Insert( menuBar->GetMenuCount()-1, menu, menuName );
    }
    else
    {
        menu = menuBar->GetMenu( menuId );
    }

    // Track down any further submenus, creating as necessary

    while( menuParts.CountTokens() > 1 )
    {
        wxMenu *submenu = 0;
        menuName = menuParts.GetNextToken();
        wxMenuItem *mi = GetMenuItemByLabel( menu, menuName, true );
        if( mi ) 
        {
            submenu=mi->GetSubMenu();
        }
        else
        {
            if( ! createParents ) return 0;
            submenu = new wxMenu;
            wxMenuItem *item=new wxMenuItem(menu,0,menuName,_T(""),wxITEM_NORMAL,submenu);
            menu->Insert(GetMenuInsertPosition(menu),item);
            menu = submenu;
        }
        menu = submenu;
    }

    // Check the item doesn't already exist

    menuName = menuParts.GetNextToken();
    if( parent ) (*parent)=menu;
    wxMenuItem *result=GetMenuItemByLabel( menu, menuName, wantMenu );
    return result;
}

bool SnapMgrScriptEnv::AddMenuItem( const wxString &name, const wxString &description, int id )
{
    wxMenu *parent;
    wxString menuName=name.AfterLast('|');
    if( menuName.IsEmpty()) return false;
    wxMenuItem *item = GetMenuItem( name, &parent, true );
    if( ! parent ) return false;

    if( item )
    {
        // Check the item doesn't already exist
        wxString message = wxString::Format(_T("Configuration error: Cannot create menu item %s of %s"),
                                            menuName.c_str(), name.c_str() );
        ::wxMessageBox( message, _T("Configuration error"), wxOK | wxICON_ERROR, frameWindow );
        return false;
    }

    parent->Insert( GetMenuInsertPosition(parent), CMD_CONFIG_BASE + id, menuName, description);
    this->Connect( CMD_CONFIG_BASE + id, wxEVT_COMMAND_MENU_SELECTED,
                wxCommandEventHandler(SnapMgrScriptEnv::OnCmdConfigMenuItem ));
    return true;
}

bool SnapMgrScriptEnv::RemoveMenuItem( const wxString &name )
{
    wxMenu *parent=0;
    wxMenuItem *item=GetMenuItem(name,&parent);
    if( ! item ) return false;
    if( item->GetSubMenu() && item->GetSubMenu()->GetMenuItemCount() > 0 ) return false;
    parent->Remove( item );
    delete item;
    return true;
}

void SnapMgrScriptEnv::EnableMenuItem( const wxString &name, bool enabled )
{
    wxMenuItem *item=GetMenuItem(name);
    if( item ) item->Enable( enabled );
}

void SnapMgrScriptEnv::OnCmdConfigMenuItem( wxCommandEvent &event )
{
    int id = event.GetId() - CMD_CONFIG_BASE;
    if( id >= 0 )
    {
        if( script ) script->RunMenuActions( id );
    }
}

// Variables used by the script

#define DEFINE_VARIABLE(vname,vvalue) \
	if( name.IsSameAs(_T(vname),false ) ) { \
		value = Value(vvalue); \
		return true; \
	    }

bool SnapMgrScriptEnv::GetValue( const wxString &name, Value &value )
{
#if defined(__WINDOWS__)
    int iswindows=1;
    int islinux=0;
#elif defined(__UNIX_LIKE__)
    int iswindows=0;
    int islinux=1;
#else
    int iswindows=0;
    int islinux=0;
#endif
    DEFINE_VARIABLE("$job_valid",(job && job->IsOk()));
    DEFINE_VARIABLE("$job_file",(job ? job->GetFilename() : wxString() ));
    DEFINE_VARIABLE("$job_title",(job ? job->Title() : wxString() ));
    DEFINE_VARIABLE("$job_path",(job ? job->GetPath() : wxString() ));
    DEFINE_VARIABLE("$snap_path",image_dir());
    DEFINE_VARIABLE("$user_config_path",user_config_dir());
    DEFINE_VARIABLE("$system_config_path", system_config_dir());
    DEFINE_VARIABLE("$coordinate_file",(job ? job->CoordinateFilename(): wxString() ));
    DEFINE_VARIABLE("$data_files",(job ? job->DataFiles() : wxString() ));
    DEFINE_VARIABLE("$load_errors",(job ? job->LoadErrors() : wxString() ));
    DEFINE_VARIABLE("$coordsys_list", GetCoordSysList() );
    DEFINE_VARIABLE("$heightref_list", GetHeightRefList() );
    DEFINE_VARIABLE("$coordsys_file", get_default_crdsys_file() );
    DEFINE_VARIABLE("$user_script_path",userScriptPath );
    DEFINE_VARIABLE("$system_script_path",scriptPath);    
    DEFINE_VARIABLE("$version",PROGRAM_VERSION);    
    DEFINE_VARIABLE("$version_date",PROGRAM_DATE);    
    DEFINE_VARIABLE("$user_id",wxGetUserId());    
    DEFINE_VARIABLE("$is_windows",iswindows ? wxString("1") : wxString(""));    
    DEFINE_VARIABLE("$is_linux",islinux ? wxString("1") : wxString(""));    
    return false;
}

// Functions used by scripts

#define DEFINE_FUNCTION(func,nprm) \
	if( _stricmp(func,functionName.c_str()) == 0 ) { \
    if( nParams != nprm )                            \
    {                                                \
        return fsBadParameters;                      \
    }     

#define DEFINE_FUNCTION2(func,nprm1,nprm2) \
	if( _stricmp(func,functionName.c_str()) == 0 ) { \
    if( nParams < nprm1 || nParams > nprm2 ) return fsBadParameters;

#define RETURN(v) \
	vresult = Value(v); \
	return fsOk; \
	}

#define CSTRPRM(i) params->AsString(i).c_str()
#define STRPRM(i)  params->AsString(i)
#define BOOLPRM(i) params->AsBool(i)

FunctionStatus SnapMgrScriptEnv::EvaluateFunction( const wxString &functionName, const Value *params, Value &vresult )
{
    Value dummy;
    int nParams=params ? params->Count() : 0;
    if( ! params ) params=&dummy;

    // Message box function

    DEFINE_FUNCTION("Message",2)
    ::wxMessageBox(STRPRM(0),STRPRM(1), wxOK | wxICON_EXCLAMATION );
    RETURN(true)

    // Query function

    DEFINE_FUNCTION("Ask",2)
    int result = ::wxMessageBox(STRPRM(0),STRPRM(1), wxYES_NO | wxICON_QUESTION );
    if( result == wxCANCEL ) return fsTerminateScript;
    RETURN( result == wxYES )

    // Get a file name

    DEFINE_FUNCTION2("GetOpenFileName",2,3)
    bool multiple = nParams == 3 && BOOLPRM(2);
    long style = wxFD_OPEN | wxFD_FILE_MUST_EXIST;
    if( multiple ) style |= wxFD_MULTIPLE;

    wxFileDialog dlg(
        frameWindow,
        STRPRM(0),
        _T("."),
        _T(""),
        STRPRM(1),
        style
    );
    wxString result;
    if( dlg.ShowModal() == wxID_OK )
    {
        if( multiple )
        {
            wxArrayString paths;
            dlg.GetPaths( paths );
            for( size_t i = 0; i < paths.Count(); i++ )
            {
                if( i > 0 ) result.Append("\n");
                result.Append( paths[i] );
            }
        }
        else
        {
            result = dlg.GetPath();
        }
    }
    RETURN( result );

    DEFINE_FUNCTION("GetSaveFileName",2)

    wxFileDialog dlg(
        frameWindow,
        STRPRM(0),
        _T("."),
        _T(""),
        STRPRM(1),
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT
    );
    wxString result;
    if( dlg.ShowModal() == wxID_OK )
    {
        result = dlg.GetPath();
    }
    RETURN( result );

    // Functions for filenames

    DEFINE_FUNCTION("FindProgram",1)
    wxPathList paths;
    paths.AddEnvList(_T("PATH"));
    wxString program = paths.FindAbsoluteValidPath(STRPRM(0));
    if( program != _T("") && !  wxFileName(program).IsFileExecutable())
    {
        program=_T(""); }
    #ifdef __WINDOWS__
    if( program == _T("") )
    {
        wxString exepath=STRPRM(0)+".exe";
        program = paths.FindAbsoluteValidPath(exepath);
    }
    #endif
    RETURN( program );

    // Find job file with specified extension

    DEFINE_FUNCTION("FindJobFile",1)
    wxString fileName;
    if( job )
    {
        wxFileName file(job->GetFilename());
        file.SetExt(STRPRM(0));
        if( file.FileExists() ) { fileName = file.GetFullName(); }
    }
    RETURN( fileName );

    DEFINE_FUNCTION("SetExtension",2)
    wxFileName file(STRPRM(0));
    file.SetExt(STRPRM(1));
    RETURN( file.GetFullPath() )

    DEFINE_FUNCTION("DirectoryExists",1)
    RETURN( ::wxDirExists( STRPRM(0)) )

    DEFINE_FUNCTION("FileExists",1)
    RETURN( ::wxFileExists( STRPRM(0)) )

    DEFINE_FUNCTION2("Filename",2,3)
    wxString result;
    wxFileName file(STRPRM(0));
    wxString part(STRPRM(1));

    if( part.IsSameAs("path",false) ) { result = file.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR); }
	else if( part.IsSameAs("directory",false) ) { result=file.GetPath(wxPATH_GET_VOLUME); }
    else if( part.IsSameAs("name",false) ) { result = file.GetName(); }
    else if( part.IsSameAs("fullname",false) ) { result = file.GetFullName(); }
    else if( part.IsSameAs("extension",false) ) { result = file.GetExt(); }
    else
    {
        wxString relative;
        if( nParams == 3 ) { relative = STRPRM(2); }
        else { relative = wxGetCwd(); }
        if( part.IsSameAs("absolute",false) )
        {
            file.MakeAbsolute( relative ); result = file.GetFullPath();
        }
        else if( part.IsSameAs("relative",false) )
        {
            file.MakeRelativeTo( relative ); result = file.GetFullPath();
        }
    }
    RETURN( result );

    DEFINE_FUNCTION2("Directory",1,2)
    wxString result;
    wxString files;
    wxString filename;
    wxString filespec = wxEmptyString;
    if( nParams == 2 ) filespec = STRPRM(1);

    wxLogNull noLog;
    wxDir dir(STRPRM(0));
    if( dir.IsOpened() )
    {
        bool found = dir.GetFirst( &files, filespec );
        while( found )
        {
            found = dir.GetNext( &filename );
            if( found ) { files.append("\n"); files.append(filename); }
        }
    }
    RETURN(files)

    // Basic file system functions

    DEFINE_FUNCTION("DeleteFile",1)
    ::wxRemoveFile( STRPRM(0));
    bool result =  ! ::wxFileExists(STRPRM(0));
    if( result && tmpFiles.Index( STRPRM(0) ) != wxNOT_FOUND ) tmpFiles.Remove( STRPRM(0));
    RETURN( result );

    DEFINE_FUNCTION("RenameFile",2)
    bool result = ::wxRenameFile( STRPRM(0), STRPRM(1), true );
    if( result && tmpFiles.Index( STRPRM(0) ) != wxNOT_FOUND ) tmpFiles.Remove( STRPRM(0));
    RETURN(	result  );

    DEFINE_FUNCTION("CopyFile",2)
    RETURN( ::wxCopyFile( STRPRM(0), STRPRM(1),true ));

    DEFINE_FUNCTION("MakeDir",1)
    RETURN( wxFileName::Mkdir( STRPRM(0), 0777, wxPATH_MKDIR_FULL ))

    DEFINE_FUNCTION("RemoveDir",1)
    RETURN( RemoveDirectory( STRPRM(0) ))
    
    DEFINE_FUNCTION("TempFile",0)
    wxString tmpFile = wxFileName::CreateTempFileName("snap.tmp");
    tmpFiles.Add( tmpFile );
    RETURN( tmpFile )

    DEFINE_FUNCTION("WriteFile",2)
    ofstream of(CSTRPRM(0));
    bool result = false;
    if( of.good() )
    {
        of << STRPRM(1);
        of.close();
        result = true;
    }
    RETURN( result )

    DEFINE_FUNCTION2("ReadFile",1,3)
    wxLogNull noLog;
    wxTextFile text(STRPRM(0));
    wxRegEx re;
    long maxLines = 0;
    bool haveRegex = nParams > 1 && re.Compile(STRPRM(1),wxRE_ADVANCED);
    if( nParams > 2 )  STRPRM(2).ToLong(&maxLines);
    wxString result;
    if( text.Open() )
    {
        long nLines = 0;
        for ( wxString str = text.GetFirstLine(); !text.Eof(); str = text.GetNextLine() )
        {
            if( haveRegex && ! re.Matches(str)) continue;
            result.Append( str );
            result.Append( "\n" );
            nLines++;
            if( maxLines && nLines >= maxLines ) break;
        }
    }
    RETURN( result )

    DEFINE_FUNCTION("AppendFile",2)
    ofstream of(CSTRPRM(0), ios::app);
    bool result = false;
    if( of.good() )
    {
        of << STRPRM(1);
        of.close();
        result = true;
    }
    RETURN( result )

    DEFINE_FUNCTION("BrowseFile",1)
    RETURN( wxLaunchDefaultBrowser( STRPRM(0), 0 ))

    //

    DEFINE_FUNCTION2("RunScript",1,2)
    wxFileName scriptFile(STRPRM(0));
	bool result = true;

    if( ! scriptFile.IsAbsolute() )
    {
		if(nParams > 1)
		{
			wxString basePath=STRPRM(1);
			if( ! wxDirExists(basePath)) 
			{
				wxFileName bfn(basePath);
				basePath=bfn.GetPath(false);
			}
            scriptFile.MakeAbsolute(basePath);
		}
		else
		{
			const char *sf=find_config_file("snapscript",STRPRM(0).c_str(),0);
			if( sf ) scriptFile=wxString(sf); else result=false;
		}
    }

    result = result && scriptFile.FileExists();
    if( result )
    {
        result = script->ExecuteScript( (const char *) scriptFile.GetFullPath().c_str() );
    }
    RETURN( result )

    // Functions to run programs

    DEFINE_FUNCTION2("Run",1,20)
    wxLogNull noLog;
    #ifdef UNIX
    wxString argv;
        for( int i = 0; i < nParams; i++ ) { if( i > 0 ) argv.Append(" "); argv.Append( STRPRM(i)); }
    #else
        char **argv = new char *[nParams+1];
        for( int i = 0; i < nParams; i++ ) { argv[i] = const_cast<char *>(CSTRPRM(i)); }
        argv[nParams] = 0;
    #endif
    frameWindow->SetCursor( *wxHOURGLASS_CURSOR );
    long result = ::wxExecute( argv, wxEXEC_SYNC );
    frameWindow->Raise();
    frameWindow->SetCursor( wxNullCursor );
    #ifndef UNIX
        delete [] argv;
    #endif
    wxString resultStr;
    if( result != -1 ) { resultStr << result; }
    RETURN( resultStr );

    DEFINE_FUNCTION2("LogRun",1,20)
    wxLogNull noLog;
    wxString command;
    for( int i = 0; i < nParams; i++ ) { if( i > 0 ) command.Append(" "); command.Append( STRPRM(i)); }
    wxArrayString output;
    wxArrayString errors;
    frameWindow->SetCursor( *wxHOURGLASS_CURSOR );
    long result = ::wxExecute( command, output, errors );
    frameWindow->Raise();
    frameWindow->SetCursor( wxNullCursor );
    wxString outputString;
    if( result != -1 )
    {
        size_t i;
        for( i = 0; i < output.Count(); i++ )
        {
            outputString.Append( output[i] );
            outputString.Append(_T("\n"));
        }
        for( i = 0; i < errors.Count(); i++ )
        {
            outputString.Append( errors[i] );
            outputString.Append(_T("\n"));
        }
        if( outputString.IsEmpty() ) { outputString = wxString("true"); }
    }
    RETURN( outputString );

    DEFINE_FUNCTION2("Start",1,20)
    wxLogNull noLog;
    char **argv = new char *[nParams+1];
    for( int i = 0; i < nParams; i++ ) { argv[i] = const_cast<char *>(CSTRPRM(i)); }
    argv[nParams] = 0;
    long result = ::wxExecute( argv, wxEXEC_ASYNC );
    delete [] argv;
    RETURN( result != 0 );

    // Configuration settings

    DEFINE_FUNCTION("GetConfig",1)
    wxConfigBase *config=wxConfigBase::Get();
    config->SetPath("/Settings");
    wxString value;
    config->Read( STRPRM(0), &value );
    RETURN( value );


    DEFINE_FUNCTION("SetConfig",2)
    wxConfigBase *config=wxConfigBase::Get();
    config->SetPath("/Settings");
    bool result = config->Write(STRPRM(0),STRPRM(1));
    RETURN( result );

	// Get a configuration file

	DEFINE_FUNCTION2("FindConfigFile",2,3)
	const char *cfgsec=STRPRM(0).c_str();
	const char *fname=STRPRM(1).c_str();
	const char *fext=nParams > 2 ? STRPRM(2).c_str() : 0;
    reset_config_dirs();
	const char *cfgfile=find_config_file(cfgsec,fname,fext);
	wxString result = cfgfile ? _T(cfgfile) : _T("");
	RETURN( result );

    // Regular expression match

    DEFINE_FUNCTION2("Match",2,3)
    wxString result;
    wxRegEx re;
    if( re.Compile(STRPRM(1),wxRE_ADVANCED) && re.Matches(STRPRM(0)) )
    {
        size_t start;
        size_t len;
        int nGroup = 0;
        long nm = 0;
        if( nParams == 3 && STRPRM(2).ToLong(&nm) ) nGroup = nm;
        re.GetMatch( &start, &len, nGroup );
        result = STRPRM(0).Mid(start,len);

        //re.GetMatch( result, 0 );
    }
    RETURN( result )
     
    // Regular expression match

    DEFINE_FUNCTION2("MatchGroups",2,3)
    Value result;
    wxString text(STRPRM(0));
    bool global=false;
    if( nParams == 3 && BOOLPRM(3)) global=true;
    wxRegEx re;
    if( re.Compile(STRPRM(1),wxRE_ADVANCED))
    {
        size_t index=0;
        while( re.Matches(text.Mid(index)) )
        {
            size_t nMatch = re.GetMatchCount();
            for( size_t nm = 1; nm < nMatch; nm++ )
            {
                if( nm == 1 ) result.SetValue(re.GetMatch(text,nm));
                else result.SetNext(re.GetMatch(text,nm));
            }
            if( ! global ) break;
            size_t start;
            size_t len;
            re.GetMatch(&start,&len);
            if( start + len <= 0 ) break;
            index += start+len;
            //re.GetMatch( result, 0 );
        }
    }
    RETURN( result ) 

    DEFINE_FUNCTION2("Replace",3,4)
    wxString result( STRPRM(0) );
    wxRegEx re;
    if( re.Compile(STRPRM(1),wxRE_ADVANCED))
    {
        int nReplace = 0;
        long nr=0;
        if( nParams == 4  && STRPRM(3).ToLong(&nr) ) { nReplace = nr; }
        if( nReplace <= 0 ) nReplace = maxReplace;
        re.Replace( &result, STRPRM(2), nReplace );
    }
    RETURN( result )

    // Date formatting

    DEFINE_FUNCTION2("GetDate",0,1)
    wxString result;
    wxString format;
    if( nParams > 0 )
    {
        format = STRPRM(0);
    }
    else
    {
        format = _T("%#d %b %Y %H:%M");
    }
    result = wxDateTime::Now().Format( format );
    RETURN( result )

	// Insert path into environment variable (default PATH)

	DEFINE_FUNCTION2("InsertPath",1,2)
	wxString pathval=STRPRM(0);
	wxString pathvar= nParams > 1 ? STRPRM(1) : _T("PATH");
	InsertPath(pathval,pathvar);
	wxString result;
	wxGetEnv(pathvar,&result);
	RETURN( result )

    // Environment variable

    DEFINE_FUNCTION("GetEnv",1)
    wxString result;
    wxGetEnv( STRPRM(0), &result );
    RETURN( result )

    // Environment variable

    DEFINE_FUNCTION("SetEnv",2)
    wxString value=STRPRM(1);
    wxSetEnv( STRPRM(0), value.c_str() );
    #ifdef __WINDOWS__
    // See comment above in SnapMgrScriptEnv::InsertPath
    wxString setenv = STRPRM(0) + _T("=") + value;
    _putenv( setenv.c_str() );
    #endif
    RETURN( value )

    // String functions

    DEFINE_FUNCTION("UpperCase",1)
    wxString value=STRPRM(0);
    RETURN( value.MakeUpper() )

    DEFINE_FUNCTION("LowerCase",1)
    wxString value=STRPRM(0);
    RETURN( value.MakeLower() )

    DEFINE_FUNCTION2("SubString",2,3)
    wxString value=STRPRM(0);
    size_t start=0;
    size_t length=wxStringBase::npos;
    long nm;
    if( STRPRM(1).ToLong(&nm) ) start = nm;
    if( nParams == 3 && STRPRM(2).ToLong(&nm) ) length = nm;
    RETURN( value.Mid(start,length) )

    // Loading and unloading job is passed back to the frame window,
    // to ensure the user interface is updated...

    DEFINE_FUNCTION("UnloadJob",0)
    bool success = UnloadJob( true );
    RETURN( success )

    DEFINE_FUNCTION("UpdateJob",0)
    bool success = UpdateJob();
    RETURN(success)

    DEFINE_FUNCTION("LoadJob",1)
    bool success = LoadJob(STRPRM(0));
    RETURN( success )

    DEFINE_FUNCTION("Log",1)
    wxLogMessage( "%s", CSTRPRM(0) );
    RETURN( true )

    DEFINE_FUNCTION("ClearLog",0)
    wxCommandEvent evt( wxEVT_SNAP_CLEARLOG );
    frameWindow->ProcessEvent(evt);
    RETURN( true )

    return fsBadFunction;
}

