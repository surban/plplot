/* $Id$
 * $Log$
 * Revision 1.25  1994/06/16 19:04:32  mjl
 * Massively restructured.  Is now just a front-end to the pltkMain()
 * function.  Structured along the preferred lines for extended wish'es.
 *
*/

/* 
 * plserver.c
 * Maurice LeBrun
 * 30-Apr-93
 *
 * Plplot graphics server.
 *
 * Just a 
 * Is typically run as a child process from the plplot TK driver to render
 * output.  Can use either TK send or Tcl-DP RPC for communication,
 * depending on how it is invoked.
 *
 * Also plserver can be used the same way as wish or dpwish, as it
 * contains the functionality of each of these (except the -notk Tcl-DP
 * command-line option is not supported).  In the source code I've changed
 * as few lines as possible from the source for "wish" in order to make it
 * easier to track future changes to Tcl/TK and Tcl-DP.  Tcl_AppInit (in
 * tkshell.c) was copied from the Tcl-DP sources and modified accordingly.
 */

#include "plserver.h"
/*
#define DEBUG
*/

/* Application-specific command-line options */
/* Variable declarations */

static char *client_name;	/* Name of client main window */
static char *auto_path;		/* addition to auto_path */
static int child;		/* set if child of TK driver */
static int pass_thru;		/* Skip normal error termination when set */
static char *cmdbuf = NULL;	/* Buffer to hold evalled commands */
static int cmdbuf_len = 100;	/* Initial command buffer length */
static int dp;			/* set if using Tcl-DP to communicate */
static char *client_host;	/* Host id for client */
static char *client_port;	/* Communications port id for client */

static Tk_ArgvInfo argTable[] = {
    {"-client_name", TK_ARGV_STRING, (char *) NULL, (char *) &client_name,
	 "Client main window name to connect to"},
    {"-client_host", TK_ARGV_STRING, (char *) NULL, (char *) &client_host,
	 "Client host to connect to"},
    {"-client_port", TK_ARGV_STRING, (char *) NULL, (char *) &client_port,
	 "Client port (Tcl-DP) to connect to"},
    {"-auto_path", TK_ARGV_STRING, (char *) NULL, (char *) &auto_path,
	 "Additional directory(s) to autoload"},
    {"-child", TK_ARGV_CONSTANT, (char *) 1, (char *) &child,
	 "Set ONLY when child of plplot TK driver"},
    {(char *) NULL, TK_ARGV_END, (char *) NULL, (char *) NULL,
	 (char *) NULL}
};

/* PLplot/Tk extension command -- handle exit. */

static int
plExitCmd(ClientData clientData, Tcl_Interp *interp, int argc, char **argv);

/* Sends specified command to client, aborting on an error. */

static void
client_cmd(Tcl_Interp *interp, char *cmd);

/* Evals the specified command, aborting on an error. */

static void
tcl_cmd(Tcl_Interp *interp, char *cmd);

/*----------------------------------------------------------------------*\
 * main --
 *
 * Just a stub routine to call pltkMain.  The latter is nice to have
 * when building extended wishes, since then you don't have to rely on
 * sucking the Tk main out of libtk (which doesn't work correctly on all
 * systems/compilers/linkers/etc).  Hopefully in the future Tk will
 * supply a sufficiently capable tkMain() type function that can be used
 * instead. 
\*----------------------------------------------------------------------*/

int
main(int argc, char **argv)
{
    int i, myargc = argc;
    char *myargv[20];
    Tcl_Interp *interp;
    char *helpmsg = "Command-specific options:";

#ifdef DEBUG
    fprintf(stderr, "Program %s called with arguments :\n", argv[0]);
    for (i = 1; i < argc; i++) 
	fprintf(stderr, "%s ", argv[i]);
    fprintf(stderr, "\n");
#endif

/* Create interpreter just for argument parsing */

    interp = Tcl_CreateInterp();

/* Save arglist to get around tk_ParseArgv limitations */

    for (i = 0; i < argc; i++)
	myargv[i] = argv[i];

/* Parse args */
/* Examine the result string to see if an error return is really an error */

    if (Tk_ParseArgv(interp, (Tk_Window) NULL, &argc, argv, argTable, 
		     TK_ARGV_NO_DEFAULTS) != TCL_OK) {
	fprintf(stderr, "\n(plserver) %s\n\n", interp->result);
	fprintf(stderr, "\
The client_<xxx> and -child options should not be used except via the\n\
Plplot/Tk driver.\n\n(wish) ");
	if (strncmp(interp->result, helpmsg, strlen(helpmsg)))
	    exit(1);
    }

/* No longer need interpreter */

    Tcl_DeleteInterp(interp);

/* Call pltkMain() with original argc/argv list, to make sure -h is seen */
/* Does not return until program exit */

    exit(pltkMain(myargc, myargv));
}


/*
 *----------------------------------------------------------------------
 *
 * Tcl_AppInit --
 *
 *	This procedure performs application-specific initialization.
 *	Most applications, especially those that incorporate additional
 *	packages, will have their own version of this procedure.
 *
 * Results:
 *	Returns a standard Tcl completion code, and leaves an error
 *	message in interp->result if an error occurs.
 *
 * Side effects:
 *	Depends on the startup script.
 *
 *----------------------------------------------------------------------
 */

int
Tcl_AppInit(interp)
    Tcl_Interp *interp;		/* Interpreter for application. */
{
    Tk_Window main;

    main = Tk_MainWindow(interp);

    /*
     * Call the init procedures for included packages.  Each call should
     * look like this:
     *
     * if (Mod_Init(interp) == TCL_ERROR) {
     *     return TCL_ERROR;
     * }
     *
     * where "Mod" is the name of the module.
     */

    if (Tcl_Init(interp) == TCL_ERROR) {
	return TCL_ERROR;
    }
    if (main && Tk_Init(interp) == TCL_ERROR) {
	return TCL_ERROR;
    }
#ifdef TCL_DP
    if (Tdp_Init(interp) == TCL_ERROR) {
	return TCL_ERROR;
    }
#endif
    if (Pltk_Init(interp) == TCL_ERROR) {
	return TCL_ERROR;
    }

/* Application-specific startup.  That means: for use in plserver ONLY. */

/* Pass child variable to interpreter if set. */

    if (child != 0)
	Tcl_SetVar(interp, "child", "1", 0);

/* If client_name is set, TK send is being used to communicate. */
/* If client_port is set, Tcl-DP RPC is being used to communicate. */
/* The "dp" variable determines which style communication is used */

    if (client_name != NULL) {
	Tcl_SetVar(interp, "client_name", client_name, 0);
	dp = 0; tcl_cmd(interp, "set dp 0");
    }
    else if (client_port != NULL) {
#ifdef TCL_DP
	Tcl_SetVar(interp, "client_port", client_port, 0);
	if (client_host != NULL)
	    Tcl_SetVar(interp, "client_host", client_host, 0);
	dp = 1; tcl_cmd(interp, "set dp 1");
#else
	Tcl_AppendResult(interp,
			 "no Tcl-DP support in this version of plserver",
			 (char *) NULL);
	return TCL_ERROR;
#endif
    }

/* Add user-specified directory(s) to auto_path */

    if (auto_path != NULL) {
	Tcl_SetVar(interp, "dir", auto_path, 0);
	tcl_cmd(interp, "set auto_path \"$dir $auto_path\"");
    }

/* Rename "exit" to "tkexit", and insert custom exit handler */

    tcl_cmd(interp, "rename exit tkexit");

    Tcl_CreateCommand(interp, "exit", plExitCmd,
                      (ClientData) main, (void (*)(ClientData)) NULL);

/* Do plserver-specific startup */

    return TCL_OK;
}

/*----------------------------------------------------------------------*\
 * plExitCmd
 *
 * PLplot/Tk extension command -- handle exit.
 * The reason for overriding the normal exit command is so we can tell the
 * client to abort.
\*----------------------------------------------------------------------*/

static int
plExitCmd(ClientData clientData, Tcl_Interp *interp, int argc, char **argv)
{
    static int pass_thru = 0;

/* Safety check for out of control code */

    if (pass_thru)
	return;

    pass_thru = 1;

/* Print error message if one given */

    if (interp->result != '\0')
	fprintf(stderr, "%s\n", interp->result);

/* If client exists, tell it to self destruct */

    if (client_name != NULL)
	client_cmd(interp, "abort");

    Tcl_Eval(interp, "after 1 tkexit");
}

/*----------------------------------------------------------------------*\
 * client_cmd
 *
 * Sends specified command to client.  Don't bother with error handling
 * since this is only used for aborts right now.  Always either send
 * command in background or continue to process events, because client may
 * be busy and unable to respond.
\*----------------------------------------------------------------------*/

static void
client_cmd(Tcl_Interp *interp, char *cmd)
{
    if (dp)
	Tcl_VarEval(interp, "dp_RPC $client -events all ",
		    cmd, (char **) NULL);
    else
	Tcl_VarEval(interp, "send $client after 1 ",
		    cmd, (char **) NULL);
}

/*----------------------------------------------------------------------*\
* tcl_cmd
*
* Evals the specified command, aborting on an error.
\*----------------------------------------------------------------------*/

static void
tcl_cmd(Tcl_Interp *interp, char *cmd)
{
    int result;

    dbug_enter("tcl_cmd");
#ifdef DEBUG_ENTER
    fprintf(stderr, "plserver: evaluating command %s\n", cmd);
#endif

    result = Tcl_VarEval(interp, cmd, (char **) NULL);
    if (result != TCL_OK) {
	Tcl_Eval(interp, "exit");
	exit(1);
    }
}
