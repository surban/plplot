/* $Id$
 * $Log$
 * Revision 1.30  1994/03/22 23:17:37  furnish
 * Avoid collision with user code when he wants to make a custom wish
 * combined with PLPLOT.
 *
 * Revision 1.29  1994/02/07  23:02:11  mjl
 * Changed to using pl_PacketSend for data transfer, which now requires no
 * communication between interpreters.  Communication parameters stored in
 * the dev->iodev structure.
 *
 * Revision 1.28  1994/02/01  22:46:23  mjl
 * Added support for starting remsh with -l <user> flag.
 *
 * Revision 1.27  1994/01/25  06:21:34  mjl
 * Removed code for default selection of background color based on display
 * type -- now handled entirely in the server.  Fixed default name for
 * container window to work when program name has a leading path
 * specification.
 *
 * Revision 1.26  1994/01/17  21:33:28  mjl
 * Robustified send commands for when interpreter name has embedded blanks
 * (as occurs when the same application is started several times, each
 * creating its own main window).
 *
 * Revision 1.25  1994/01/15  17:46:48  mjl
 * Converted to new PDF function call syntax.  Substantially changed server
 * startup code -- now can handle a variety of cases including starting
 * plserver on a remote node (via remsh) or plserver already existing and
 * only needing to be contacted.  Rewrote data channel code to use socket
 * when DP driver is used.
 *
 * Revision 1.24  1993/12/22  23:09:53  mjl
 * Changes so that TK driver no longer times out on slow network connections
 * (it's just rreeaaalllyyyy ssllooowwww).  Where timeouts are a problem,
 * the client must issue the command to the server without waiting for a
 * reply, and then sit in TK wait loop until some specified condition is
 * met (usually the server sets a client interpreter variable when done).
 *
 * Revision 1.23  1993/12/21  10:30:24  mjl
 * Changed to separate initialization routines for dp vs tk drivers.
 * Reworked server_cmd function to work well with both Tcl-DP and TK send;
 * also method for putting commands in the background is better thought out
 * (and works better).  When using Tcl-DP for communication, the TK main
 * window is NOT created now.  This is a bit tricky since certain commands
 * no longer work if you don't have a main window -- like "tkwait", "update",
 * and "after", and alternate methods must be used to get the same effects.
 *
 * Revision 1.22  1993/12/15  09:04:31  mjl
 * Added support for Tcl-DP style communication.  Many small tweaks to
 * driver-plserver interactions made.  server_cmdbg() added for sending
 * commands to the server in the background (infrequently used because it
 * does not intercept errors).
 *
 * Revision 1.21  1993/12/09  21:19:40  mjl
 * Changed call syntax for tk_toplevel().
 *
 * Revision 1.20  1993/12/09  20:35:14  mjl
 * Fixed some casts.
 *
 * Revision 1.19  1993/12/08  06:18:09  mjl
 * Changed to include new plplotX.h header file.
 *
 * Revision 1.18  1993/12/06  07:43:11  mjl
 * Fixed bogus tmpnam call.
 *
 * Revision 1.17  1993/11/19  07:31:36  mjl
 * Updated to new call syntax for tk_toplevel().
 *
 * Revision 1.16  1993/11/15  08:31:16  mjl
 * Now uses tmpnam() to get temporary file instead of tempnam().  Also,
 * put in rename of dangerous Tcl commands just after startup.
 *
 * Revision 1.15  1993/11/07  09:02:52  mjl
 * Added escape function handling for dealing with flushes.
*/

/*	tk.c
*
*	Maurice LeBrun
*	30-Apr-93
*
*	PLPLOT TCL/TK device driver.
*
*	Passes graphics commands to renderer and certain X
*	events back to user if requested.
*/

/*
#define DEBUG
#define DEBUG_ENTER
*/

#ifdef TK

#include "plserver.h"
#include "drivers.h"
#include "metadefs.h"
#include "plevent.h"
#include <errno.h>

/* If set, BUFFER_FIFO causes FIFO i/o to be buffered */

#define BUFFER_FIFO 0

/* A handy command wrapper */

#define tk_wr(code) \
if (code) { abort_session(pls, "Unable to write to pipe"); }

/* Use vfork() on some systems */

#ifndef FORK
#define FORK fork
#endif

/* INDENT OFF */
/*----------------------------------------------------------------------*/
/* Struct to hold device-specific info. */

typedef struct {
    Tk_Window w;		/* Main window */
    Tcl_Interp *interp;		/* Interpreter */
    short xold, yold;		/* Coordinates of last point plotted */
    int   exit_eventloop;	/* Flag for breaking out of event loop */
    int   pass_thru;		/* Skips normal error termination when set */
    char  *cmdbuf;		/* Command buffer */
    int   cmdbuf_len;		/* and its length */
    PLiodev *iodev;		/* I/O device info */
} TkDev;

/* Function prototypes */

static void  init		(PLStream *);
static void  tk_start		(PLStream *);
static void  tk_stop		(PLStream *);
static void  tk_di		(PLStream *);
static void  WaitForPage	(PLStream *);
static void  HandleEvents	(PLStream *);
static void  tk_configure	(PLStream *);
static void  init_server	(PLStream *);
static void  launch_server	(PLStream *);
static void  flush_output	(PLStream *);
static void  plwindow_init	(PLStream *);
static void  link_init		(PLStream *);

/* Tcl/TK utility commands */

static void  tk_wait		(PLStream *, char *);
static void  abort_session	(PLStream *, char *);
static void  server_cmd		(PLStream *, char *, int);
static void  tcl_cmd		(PLStream *, char *);
static int   tcl_eval		(PLStream *, char *);
static void  copybuf		(PLStream *pls, char *cmd);

/* These are internal TCL commands */

static int   Abort		(ClientData, Tcl_Interp *, int, char **);
static int   KeyEH		(ClientData, Tcl_Interp *, int, char **);

/* INDENT ON */
/*----------------------------------------------------------------------*\
* plD_init_dp()
* plD_init_tk()
* init_tk()
*
* Initialize device.
* TK-dependent stuff done in tk_start().  You can set the display by
* calling plsfnam() with the display name as the (string) argument.
\*----------------------------------------------------------------------*/

void
plD_init_tk(PLStream *pls)
{
    pls->dp = 0;
    init(pls);
}

void
plD_init_dp(PLStream *pls)
{
#ifdef TCL_DP
    pls->dp = 1;
#else
    fprintf(stderr, "The Tcl-DP driver hasn't been installed!\n");
    pls->dp = 0;
#endif
    init(pls);
}

static void
tk_wr_header(PLStream *pls, char *header)
{
    tk_wr( pdf_wr_header(pls->pdfs, header) );
    pls->bytecnt += strlen(header)+1;
}

static void
init(PLStream *pls)
{
    U_CHAR c = (U_CHAR) INITIALIZE;
    TkDev *dev;
    int xmin = 0;
    int xmax = PIXELS_X - 1;
    int ymin = 0;
    int ymax = PIXELS_Y - 1;

    float pxlx = (double) PIXELS_X / (double) LPAGE_X;
    float pxly = (double) PIXELS_Y / (double) LPAGE_Y;

    dbug_enter("plD_init_tk");

    pls->termin = 1;		/* is an interactive terminal */
    pls->icol0 = 1;
    pls->width = 1;
    pls->bytecnt = 0;
    pls->page = 0;
    pls->dev_di = 1;
    pls->dev_flush = 1;		/* Want to handle our own flushes */

/* Specify buffer size if not yet set (can be changed by -bufmax option).  */
/* A small buffer works best for socket communication */

    if (pls->bufmax == 0) {
	if (pls->dp)
	    pls->bufmax = 450;
	else
	    pls->bufmax = 3500;
    }

/* Allocate and initialize device-specific data */

    if (pls->dev != NULL)
	free((void *) pls->dev);

    pls->dev = calloc(1, (size_t) sizeof(TkDev));
    if (pls->dev == NULL)
	plexit("plD_init_tk: Out of memory.");

    dev = (TkDev *) pls->dev;

    dev->iodev = calloc(1, (size_t) sizeof(PLiodev));
    if (dev->iodev == NULL)
	plexit("plD_init_tk: Out of memory.");

    dev->exit_eventloop = 0;

/* Start interpreter and spawn server process */

    tk_start(pls);

/* Get ready for plotting */

    dev->xold = UNDEFINED;
    dev->yold = UNDEFINED;

    plP_setpxl(pxlx, pxly);
    plP_setphy(xmin, xmax, ymin, ymax);

/* Send init info */

    tk_wr( pdf_wr_1byte(pls->pdfs, c) );
    pls->bytecnt++;

/* The header and version fields will be useful when the client & server */
/* reside on different machines */

    tk_wr_header(pls, PLSERV_HEADER);
    tk_wr_header(pls, PLSERV_VERSION);

    tk_wr_header(pls, "xmin");
    tk_wr( pdf_wr_2bytes(pls->pdfs, (U_SHORT) xmin) );
    pls->bytecnt += 2;

    tk_wr_header(pls, "xmax");
    tk_wr( pdf_wr_2bytes(pls->pdfs, (U_SHORT) xmax) );
    pls->bytecnt += 2;

    tk_wr_header(pls, "ymin");
    tk_wr( pdf_wr_2bytes(pls->pdfs, (U_SHORT) ymin) );
    pls->bytecnt += 2;

    tk_wr_header(pls, "ymax");
    tk_wr( pdf_wr_2bytes(pls->pdfs, (U_SHORT) ymax) );
    pls->bytecnt += 2;

    tk_wr_header(pls, "");

/* Good place to make sure the data transfer is working OK */

    flush_output(pls);
}

/*----------------------------------------------------------------------*\
* plD_line_tk()
*
* Draw a line in the current color from (x1,y1) to (x2,y2).
\*----------------------------------------------------------------------*/

void
plD_line_tk(PLStream *pls, short x1, short y1, short x2, short y2)
{
    U_CHAR c;
    U_SHORT xy[4];
    static long count = 0, max_count = 100;
    TkDev *dev = (TkDev *) pls->dev;

    if ( ++count/max_count >= 1 ) {
	count = 0;
	HandleEvents(pls);	/* Check for events */
    }

    if (x1 == dev->xold && y1 == dev->yold) {
	c = (U_CHAR) LINETO;
	tk_wr( pdf_wr_1byte(pls->pdfs, c) );
	pls->bytecnt += 1;

	xy[0] = x2;
	xy[1] = y2;
	tk_wr( pdf_wr_2nbytes(pls->pdfs, xy, 2) );
	pls->bytecnt += 4;
    }
    else {
	c = (U_CHAR) LINE;
	tk_wr( pdf_wr_1byte(pls->pdfs, c) );
	pls->bytecnt += 1;

	xy[0] = x1;
	xy[1] = y1;
	xy[2] = x2;
	xy[3] = y2;
	tk_wr( pdf_wr_2nbytes(pls->pdfs, xy, 4) );
	pls->bytecnt += 8;
    }
    dev->xold = x2;
    dev->yold = y2;

    if (pls->bytecnt > pls->bufmax)
	flush_output(pls);
}

/*----------------------------------------------------------------------*\
* plD_polyline_tk()
*
* Draw a polyline in the current color from (x1,y1) to (x2,y2).
\*----------------------------------------------------------------------*/

void
plD_polyline_tk(PLStream *pls, short *xa, short *ya, PLINT npts)
{
    U_CHAR c = (U_CHAR) POLYLINE;
    static long count = 0, max_count = 100;
    TkDev *dev = (TkDev *) pls->dev;

    if ( ++count/max_count >= 1 ) {
	count = 0;
	HandleEvents(pls);	/* Check for events */
    }

    tk_wr( pdf_wr_1byte(pls->pdfs, c) );
    tk_wr( pdf_wr_2bytes(pls->pdfs, (U_SHORT) npts) );
    pls->bytecnt += 3;

    tk_wr( pdf_wr_2nbytes(pls->pdfs, (U_SHORT *) xa, npts) );
    tk_wr( pdf_wr_2nbytes(pls->pdfs, (U_SHORT *) ya, npts) );
    pls->bytecnt += 4*npts;

    dev->xold = xa[npts - 1];
    dev->yold = ya[npts - 1];

    if (pls->bytecnt > pls->bufmax)
	flush_output(pls);
}

/*----------------------------------------------------------------------*\
* plD_eop_tk()
*
* End of page.  
* User must hit <RETURN> to continue.
\*----------------------------------------------------------------------*/

void
plD_eop_tk(PLStream *pls)
{
    U_CHAR c = (U_CHAR) EOP;

    dbug_enter("plD_eop_tk");

    if (pls->nopause)
	return;

    tk_wr( pdf_wr_1byte(pls->pdfs, c) );
    pls->bytecnt += 1;
    WaitForPage(pls);
}

/*----------------------------------------------------------------------*\
* plD_bop_tk()
*
* Set up for the next page.
\*----------------------------------------------------------------------*/

void
plD_bop_tk(PLStream *pls)
{
    U_CHAR c = (U_CHAR) BOP;
    TkDev *dev = (TkDev *) pls->dev;

    dbug_enter("plD_bop_tk");

    dev->xold = UNDEFINED;
    dev->yold = UNDEFINED;
    pls->page++;
    tk_wr( pdf_wr_1byte(pls->pdfs, c) );
    pls->bytecnt += 1;
}

/*----------------------------------------------------------------------*\
* plD_tidy_tk()
*
* Close graphics file
\*----------------------------------------------------------------------*/

void
plD_tidy_tk(PLStream *pls)
{
    TkDev *dev = (TkDev *) pls->dev;

    dbug_enter("plD_tidy_tk");

    tk_stop(pls);
    pls->fileset = 0;
    pls->page = 0;
    free_mem(dev->cmdbuf);
}

/*----------------------------------------------------------------------*\
* plD_state_tk()
*
* Handle change in PLStream state (color, pen width, fill attribute, etc).
\*----------------------------------------------------------------------*/

void 
plD_state_tk(PLStream *pls, PLINT op)
{
    U_CHAR c = (U_CHAR) CHANGE_STATE;

    dbug_enter("plD_state_tk");

    tk_wr( pdf_wr_1byte(pls->pdfs, c) );
    pls->bytecnt += 1;

    switch (op) {

    case PLSTATE_WIDTH:
	tk_wr( pdf_wr_1byte(pls->pdfs, op) );
	tk_wr( pdf_wr_2bytes(pls->pdfs, (U_SHORT) (pls->width)) );
	pls->bytecnt += 3;
	break;

    case PLSTATE_COLOR0:
	tk_wr( pdf_wr_1byte(pls->pdfs, op) );
	tk_wr( pdf_wr_1byte(pls->pdfs, (U_CHAR) pls->icol0) );
	pls->bytecnt += 2;
	if (pls->icol0 == PL_RGB_COLOR) {
	    tk_wr( pdf_wr_1byte(pls->pdfs, pls->curcolor.r) );
	    tk_wr( pdf_wr_1byte(pls->pdfs, pls->curcolor.g) );
	    tk_wr( pdf_wr_1byte(pls->pdfs, pls->curcolor.b) );
	    pls->bytecnt += 3;
	}
	break;

    case PLSTATE_COLOR1:
	break;
    }

    if (pls->bytecnt > pls->bufmax)
	flush_output(pls);
}

/*----------------------------------------------------------------------*\
* plD_esc_tk()
*
* Escape function.
\*----------------------------------------------------------------------*/

void
plD_esc_tk(PLStream *pls, PLINT op, void *ptr)
{
    U_CHAR c = (U_CHAR) ESCAPE;

    dbug_enter("plD_esc_tk");

    tk_wr( pdf_wr_1byte(pls->pdfs, c) );
    pls->bytecnt += 1;

    tk_wr( pdf_wr_1byte(pls->pdfs, op) );
    pls->bytecnt += 1;

    switch (op) {
      case PLESC_DI:
	tk_di(pls);
	break;

      case PLESC_FLUSH:
	flush_output(pls);
	break;
    }
}

/*----------------------------------------------------------------------*\
* tk_di
*
* Process driver interface command.
* Just send the command to the remote plplot library.
\*----------------------------------------------------------------------*/

static void
tk_di(PLStream *pls)
{
    TkDev *dev = (TkDev *) pls->dev;
    char str[10];

    dbug_enter("tk_di");

/* Safety feature, should never happen */

    if (dev == NULL) 
	plexit("tk_di: Illegal call to driver (not yet initialized)");

/* Flush the buffer before proceeding */

    flush_output(pls);

/* Change orientation */

    if (pls->difilt & PLDI_ORI) {
	sprintf(str, "%f", pls->diorot);
	Tcl_SetVar(dev->interp, "rot", str, 0);

	server_cmd( pls, "$plwidget cmd setopt -ori $rot", 1 );
	pls->difilt &= ~PLDI_ORI;
    }

/* Change window into plot space */

    if (pls->difilt & PLDI_PLT) {
	sprintf(str, "%f", pls->dipxmin);
	Tcl_SetVar(dev->interp, "xl", str, 0);
	sprintf(str, "%f", pls->dipymin);
	Tcl_SetVar(dev->interp, "yl", str, 0);
	sprintf(str, "%f", pls->dipxmax);
	Tcl_SetVar(dev->interp, "xr", str, 0);
	sprintf(str, "%f", pls->dipymax);
	Tcl_SetVar(dev->interp, "yr", str, 0);

	server_cmd( pls, "$plwidget cmd setopt -wplt $xl,$yl,$xr,$yr", 1 );
	pls->difilt &= ~PLDI_PLT;
    }

/* Change window into device space */

    if (pls->difilt & PLDI_DEV) {
	sprintf(str, "%f", pls->mar);
	Tcl_SetVar(dev->interp, "mar", str, 0);
	sprintf(str, "%f", pls->aspect);
	Tcl_SetVar(dev->interp, "aspect", str, 0);
	sprintf(str, "%f", pls->jx);
	Tcl_SetVar(dev->interp, "jx", str, 0);
	sprintf(str, "%f", pls->jy);
	Tcl_SetVar(dev->interp, "jy", str, 0);

	server_cmd( pls, "$plwidget cmd setopt -mar $mar", 1 );
	server_cmd( pls, "$plwidget cmd setopt -a $aspect", 1 );
	server_cmd( pls, "$plwidget cmd setopt -jx $jx", 1 );
	server_cmd( pls, "$plwidget cmd setopt -jy $jy", 1 );
	pls->difilt &= ~PLDI_DEV;
    }

/* Update view */

    server_cmd( pls, "update", 1 );
    server_cmd( pls, "plw_update_view $plwindow", 1 );
}

/*----------------------------------------------------------------------*\
* tk_start
*
* Create TCL interpreter and spawn off server process.
* Each stream that uses the tk driver gets its own interpreter.
\*----------------------------------------------------------------------*/

static void
tk_start(PLStream *pls)
{
    TkDev *dev = (TkDev *) pls->dev;

    dbug_enter("tk_start");

/* Instantiate a TCL interpreter, and get rid of the exec command */

    dev->interp = Tcl_CreateInterp();
    tcl_cmd(pls, "rename exec {}");

/* Initialize top level window */
/* Request pls->program (if set) for the main window name */

    if (pls->program == NULL)
	pls->program = "plclient";

    if (pls->dp) {
	Tcl_SetVar(dev->interp, "dp", "1", TCL_GLOBAL_ONLY);
    }
    else {
	Tcl_SetVar(dev->interp, "dp", "0", TCL_GLOBAL_ONLY);
	if (tk_toplevel(&dev->w, dev->interp, pls->FileName, pls->program,
			pls->program))
	    abort_session(pls, "Unable to create top-level window");
    }

/* Initialize interpreter */

    tk_configure(pls);
    Tcl_SetVar(dev->interp, "tcl_interactive", "0", TCL_GLOBAL_ONLY);

/* Eval startup procs */

    if (plTcl_AppInit(dev->interp) != TCL_OK) {
	abort_session(pls, "");
    }

/* Other initializations. */
/* Autoloaded, so the user can customize it if desired */

    tcl_cmd(pls, "plclient_init"); 

/* Initialize server process */

    init_server(pls);

/* By now we should be done with all autoloaded procs, so blow away */
/* the open command just in case security has been compromised */

    tcl_cmd(pls, "rename open {}");
    tcl_cmd(pls, "rename rename {}");

/* Initialize widgets */

    plwindow_init(pls);

/* Initialize data link */

    link_init(pls);

    return;
}

/*----------------------------------------------------------------------*\
* tk_stop
*
* Normal termination & cleanup.
\*----------------------------------------------------------------------*/

static void
tk_stop(PLStream *pls)
{
    TkDev *dev = (TkDev *) pls->dev;

    dbug_enter("tk_stop");

/* Safety check for out of control code */

    if (dev->pass_thru)
	return;

    dev->pass_thru = 1;

/* Terminate data stream */

    pdf_close(pls->pdfs);

/* Kill plserver */

    if (Tcl_GetVar(dev->interp, "server", TCL_GLOBAL_ONLY) != NULL) {
	server_cmd( pls, "$plw_end_proc $plwindow", 1 );
	tcl_cmd(pls, "unset server");
    }

/* Blow away main window */

    if ( ! pls->dp)
	tcl_cmd(pls, "destroy .");

/* Blow away interpreter if it exists */

    if (dev->interp != NULL) {
	Tcl_DeleteInterp(dev->interp);
	dev->interp = NULL;
    }
}

/*----------------------------------------------------------------------*\
* abort_session
*
* Terminates with an error.  
* Cleanup is done by plD_tidy_tk(), called by plexit().
\*----------------------------------------------------------------------*/

static void
abort_session(PLStream *pls, char *msg)
{
    dbug_enter("abort_session");

    pls->nopause = TRUE;
    plexit(msg);
}

/*----------------------------------------------------------------------*\
* tk_configure
*
* Does global variable & command initialization, mostly for interpreter.
\*----------------------------------------------------------------------*/

static void
tk_configure(PLStream *pls)
{
    TkDev *dev = (TkDev *) pls->dev;

    dbug_enter("tk_configure");

/* Tell interpreter about commands. */

    Tcl_CreateCommand(dev->interp, "abort", Abort,
		      (ClientData) pls, (void (*) (ClientData)) NULL);

    Tcl_CreateCommand(dev->interp, "keypress", KeyEH,
		      (ClientData) pls, (void (*) (ClientData)) NULL);

/* Set some relevant interpreter variables */

    if (! pls->dp) 
	tcl_cmd(pls, "set client_name [winfo name .]");

    if (pls->server_name != NULL)
	Tcl_SetVar(dev->interp, "server_name", pls->server_name, 0);

    if (pls->server_host != NULL)
	Tcl_SetVar(dev->interp, "server_host", pls->server_host, 0);

    if (pls->server_port != NULL)
	Tcl_SetVar(dev->interp, "server_port", pls->server_port, 0);
}

/*----------------------------------------------------------------------*\
* init_server
*
* Starts interaction with server process, launching it if necessary.
*
* There are several possibilities we must account for, depending on the
* message protocol, input flags, and whether plserver is already running
* or not.  From the point of view of the code, they are:
*
*    1. Driver: tk
*	Flags: <none>
*	Meaning: need to start up plserver (same host)
*	Actions: fork plserver, passing it our TK main window name
*		 for communication.  Once started, plserver will send
*		 back its main window name.
* 
*    2. Driver: dp
*	Flags: <none>
*	Meaning: need to start up plserver (same host)
*	Actions: fork plserver, passing it our Tcl-DP communication port
*		 for communication. Once started, plserver will send
*		 back its created message port number.
* 
*    3. Driver: tk
*	Flags: -server_name
*	Meaning: plserver already running (same host)
*	Actions: communicate to plserver our TK main window name.
* 
*    4. Driver: dp
*	Flags: -server_port
*	Meaning: plserver already running (same host)
*	Actions: communicate to plserver our Tcl-DP port number.
* 
*    5. Driver: dp
*	Flags: -server_host
*	Meaning: need to start up plserver (remote host)
*	Actions: remsh (rsh) plserver, passing it our host ID and Tcl-DP
*		 port for communication. Once started, plserver will send
*		 back its created message port number.
* 
*    6. Driver: dp
*	Flags: -server_host -server_port
*	Meaning: plserver already running (remote host)
*	Actions: communicate to remote plserver our host ID and Tcl-DP
*		 port number.
*
* For a bit more flexibility, you can change the name of the process
* invoked from "plserver" to something else, using the -plserver flag.
* 
* The startup procedure involves some rather involved handshaking 
* between client and server.  This is made easier by using the Tcl
* variables:
*
*	client_host client_port server_host server_port 
*
* when using Tcl-DP sends and
*
*	client_name server_name
*
* when using TK sends.  The global Tcl variables 
*
*	client server
*
* are used for the defining identification for the client and server,
* respectively -- they denote the main window name when TK sends are used
* and the respective process's listening socket when Tcl-DP sends are
* used.  Note that in the former case, $client is just the same as
* $client_name.  In addition, since the server may need to communicate
* with many different processes, every command to the server contains the
* sender's client id (so it knows how to report back if necessary).  Thus
* this interpreter must know both $server as well as $client.  It is most
* convenient to set $client from the server, as a way to signal that
* communication has been set up and it is safe to proceed.
*
* Often it is necessary to use constructs such as [list $server] instead
* of just $server.  This occurs since you could have multiple copies
* running on the display (resulting in names of the form "plserver #2",
* etc).
\*----------------------------------------------------------------------*/

static void
init_server(PLStream *pls)
{
    TkDev *dev = (TkDev *) pls->dev;
    int server_exists = 0;

    dbug_enter("init_server");

#ifdef DEBUG
    fprintf(stderr, "%s -- PID: %d, PGID: %d, PPID: %d\n",
	    __FILE__, getpid(), getpgrp(), getppid());
#endif

/* If no means of communication provided, need to launch plserver */

    if (( ! pls->dp && pls->server_name != NULL ) ||
	(   pls->dp && pls->server_port != NULL ) )
	server_exists = 1;

/* So launch it */

    if ( ! server_exists)
	launch_server(pls);

/* Set up communication channel to server */

    if (pls->dp) {
	tcl_cmd(pls,
		"set server [dp_MakeRPCClient $server_host $server_port]");
    }
    else {
	tcl_cmd(pls, "set server $server_name");
    }

/* If server didn't need launching, contact it here */

    if (server_exists)
	tcl_cmd(pls, "plclient_link_init"); 
}

/*----------------------------------------------------------------------*\
* launch_server
*
* Launches plserver, locally or remotely.
\*----------------------------------------------------------------------*/

static void
launch_server(PLStream *pls)
{
    TkDev *dev = (TkDev *) pls->dev;
    char *argv[20], *plserver_exec, *ptr;
    int i;
    pid_t pid;

    dbug_enter("launch_server");

    if (pls->plserver == NULL) 
	pls->plserver = "plserver";

/* Build argument list */

    i = 0;

/* If we're doing a remsh, need to set up its arguments first. */

    if ( pls->dp && pls->server_host != NULL ) {
	argv[i++] = pls->server_host;	/* Host name for remsh */

	if (pls->user != NULL) {
	    argv[i++] = "-l";
	    argv[i++] = pls->user; 	/* User name on remote node */
	}
    }

/* The invoked executable name comes next */

    argv[i++] = pls->plserver;

/* The rest are arguments to plserver */

    argv[i++] = "-child";		/* Tell plserver its ancestry */

    if (pls->auto_path != NULL) {
	argv[i++] = "-auto_path";	/* Additional directory(s) */
	argv[i++] = pls->auto_path;	/* to autoload */
    }

    if (pls->geometry != NULL) {
	argv[i++] = "-geometry";	/* Top level window geometry */
	argv[i++] = pls->geometry;
    }

/* If communicating via Tcl-DP, specify communications port id */
/* If communicating via TK send, specify main window name */

    if (pls->dp) {
	argv[i++] = "-client_host";
	argv[i++] = Tcl_GetVar(dev->interp, "client_host", TCL_GLOBAL_ONLY);

	argv[i++] = "-client_port";
	argv[i++] = Tcl_GetVar(dev->interp, "client_port", TCL_GLOBAL_ONLY);

	if (pls->user != NULL) {
	    argv[i++] = "-l";
	    argv[i++] = pls->user;
	}
    }
    else {
	argv[i++] = "-client_name";
	argv[i++] = Tcl_GetVar(dev->interp, "client_name", TCL_GLOBAL_ONLY);
    }

/* The display absolutely must be set if invoking a remote server (by remsh) */
/* Use the DISPLAY environmental, if set.  Otherwise use the remote host. */

    if (pls->FileName != NULL) {
	argv[i++] = "-display";
	argv[i++] = pls->FileName;
    }
    else if ( pls->dp && pls->server_host != NULL ) {
	argv[i++] = "-display";
	if ((ptr = getenv("DISPLAY")) != NULL)
	    argv[i++] = ptr;
	else
	    argv[i++] = "unix:0.0";
    }

/* Add terminating null */

#ifdef DEBUG
    {
	int j;
	fprintf(stderr, "argument list: \n   ");
	for (j = 0; j < i; j++) 
	    fprintf(stderr, "%s ", argv[j]);
	fprintf(stderr, "\n");
    }
#endif
    argv[i++] = NULL;

/* Start server process */
/* It's a fork/remsh if on a remote machine */

    if ( pls->dp && pls->server_host != NULL ) {
	if ((pid = FORK()) < 0) {
	    abort_session(pls, "Unable to fork server process");
	}
	else if (pid == 0) {
	    fprintf(stderr, "Starting up %s on node %s\n", pls->plserver,
		    pls->server_host);

	    if (execvp("remsh", argv)) {
		perror("Unable to exec server process");
		_exit(1);
	    }
	}
    }

/* Running locally, so its a fork/exec */

    else {
	plserver_exec = plFindCommand(pls->plserver);
	if ( (plserver_exec == NULL) || (pid = FORK()) < 0) {
	    abort_session(pls, "Unable to fork server process");
	}
	else if (pid == 0) {
	    fprintf(stderr, "Starting up %s\n", plserver_exec);
	    if (execv(plserver_exec, argv)) {
		fprintf(stderr, "Unable to exec server process.\n");
		_exit(1);
	    }
	}
	free_mem(plserver_exec);
    }

/* Wait for server to set up return communication channel */

    tk_wait(pls, "[info exists client]" );
}

/*----------------------------------------------------------------------*\
* plwindow_init
*
* Configures the widget hierarchy we are sending the data stream to.  
*
* If a widget name (identifying the actual widget or a container widget)
* hasn't been supplied already we assume it needs to be created.
*
* In order to achieve maximum flexibility, the plplot tk driver requires
* only that certain TCL procs must be defined in the server interpreter.
* These can be used to set up the desired widget configuration.  The procs
* invoked from this driver currently include:
*
*    $plw_create_proc		Creates the widget environment
*    $plw_init_proc		Initializes the widget(s)
*    $plw_start_proc		Does any remaining startup necessary
*    $plw_end_proc		Prepares for shutdown
*    $plw_flash_proc		Invoked when waiting for page advance
*
* Since all of these are interpreter variables, they can be trivially
* changed by the user.
*
* Each of these utility procs is called with a widget name ($plwindow)
* as argument.  "plwindow" is set from the value of pls->plwindow, and
* if null is generated from the name of the client main window (to
* ensure uniqueness).  $plwindow usually indicates the container frame
* for the actual plplot widget, but can be arbitrary -- as long as the
* usage in all the TCL procs is consistent.
*
* In order that the TK driver be able to invoke the actual plplot
* widget, the proc "$plw_init_proc" deposits the widget name in the local
* interpreter variable "plwidget".
*
* In addition, the name of the client main window is given as (2nd)
* argument to "$plw_init_proc".  
\*----------------------------------------------------------------------*/

static void
plwindow_init(PLStream *pls)
{
    TkDev *dev = (TkDev *) pls->dev;
    char str[10], *pname;
    int i;

    dbug_enter("plwindow_init");

/* If widget does not exist we must create it */

    if (pls->plwindow == NULL) {

/* Give window a name */
/* Eliminate any leading path specification */

	pls->plwindow = (char *)
	    malloc(10+(strlen(pls->program)) * sizeof(char));

	pname = strrchr(pls->program, '/');
	if (pname != NULL) 
	    pname++;
	else
	    pname = pls->program;

/* Ensure that multiple widgets created by multiple streams have unique */
/* names (in case this kind of capability is someday supported) */

	if (pls->ipls == 0)
	    sprintf(pls->plwindow, ".%s", pname, pls->ipls);
	else
	    sprintf(pls->plwindow, ".%s_%d", pname, pls->ipls);

/* Replace any blanks with underscores to avoid quoting problems. */

	for (i = 0; i < strlen(pls->plwindow); i++) {
	    if (pls->plwindow[i] == ' ')
		pls->plwindow[i] = '_';
	}

/* Finally, the baby has a name. */

	Tcl_SetVar(dev->interp, "plwindow", pls->plwindow, 0);

/* Create the plframe widget & anything else you want with it. */

	server_cmd( pls, "$plw_create_proc $plwindow", 0 );
    }
    else {
	Tcl_SetVar(dev->interp, "plwindow", pls->plwindow, 0);
    }

/* Initialize the widget(s) */

    server_cmd( pls, "$plw_init_proc $plwindow [list $client]", 1 );
    tk_wait(pls, "[info exists plwidget]" );

/* Now we should have the actual plplot widget name in $plwidget */
/* Configure remote plplot stream. */

/* Configure background color if set */
/* The default color is handled from a resource setting in plconfig.tcl */

    if (pls->bgcolorset) {
	long bg;

	bg = (((pls->bgcolor.r << 8) | pls->bgcolor.g) << 8) | pls->bgcolor.b;
	sprintf(str, "#%06x", (bg & 0xFFFFFF));
	Tcl_SetVar(dev->interp, "bg", str, 0);
	server_cmd( pls, "$plwidget configure -bg $bg", 0 );
    }

/* nopixmap option */

    if (pls->nopixmap) 
	server_cmd( pls, "$plwidget cmd setopt -nopixmap", 0 );

/* Start up remote plplot */

    server_cmd( pls, "$plw_start_proc $plwindow [list $client]", 1 );
    tk_wait(pls, "[info exists widget_is_ready]" );
}

/*----------------------------------------------------------------------*\
* link_init
*
* Initializes the link between the client and the plplot widget for
* data transfer.  Defaults to a FIFO when the TK driver is selected and
* a socket when the DP driver is selected.
\*----------------------------------------------------------------------*/

static void
link_init(PLStream *pls)
{
    TkDev *dev = (TkDev *) pls->dev;
    PLiodev *iodev = (PLiodev *) dev->iodev;
    long bufmax = pls->bufmax * 1.2;

    dbug_enter("link_init");

/* Create FIFO for data transfer to the plframe widget */

    if ( ! pls->dp) {

	iodev->filename = (char *) tmpnam(NULL);
	if (mkfifo(iodev->filename, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH) < 0) 
	    abort_session(pls, "mkfifo error");

/* Tell plframe widget to open FIFO (for reading). */

	Tcl_SetVar(dev->interp, "fifoname", iodev->filename, 0);
	server_cmd( pls, "$plwidget openlink fifo $fifoname", 1 );

/* Open the FIFO for writing */
/* This will block until the server opens it for reading */

	if ((iodev->fd = open(iodev->filename, O_WRONLY)) == -1) 
	    abort_session(pls, "Error opening fifo for write");

/* Create stream interface (C file handle) to FIFO */

	iodev->type = 0;
	iodev->typename = "fifo";
	iodev->file = fdopen(iodev->fd, "wb");

/* Unlink FIFO so that it isn't left around if program crashes. */
/* This also ensures no other program can mess with it. */

	if (unlink(iodev->filename) == -1) 
	    abort_session(pls, "Error removing fifo");
    }

/* Create socket for data transfer to the plframe widget */

    else {

	iodev->type = 1;
	iodev->typename = "socket";
	tcl_cmd(pls, "plclient_dp_init");
	iodev->filehandle = Tcl_GetVar(dev->interp, "data_sock", 0);

	if (Tcl_GetOpenFile(dev->interp, iodev->filehandle,
			    0, 1, &iodev->file) != TCL_OK) {

	    fprintf(stderr, "Cannot get file info:\n\t %s\n",
		    dev->interp->result);
	    abort_session(pls, "");
	}
	iodev->fd = fileno(iodev->file);
    }

/* Create data buffer */

    pls->pdfs = pdf_bopen( NULL, bufmax );
}

/*----------------------------------------------------------------------*\
* WaitForPage()
*
* Waits for a page advance.
\*----------------------------------------------------------------------*/

static void
WaitForPage(PLStream *pls)
{
    TkDev *dev = (TkDev *) pls->dev;

    dbug_enter("WaitForPage");

    if (pls->bytecnt > 0) 
	flush_output(pls);

    while ( ! dev->exit_eventloop)
	Tk_DoOneEvent(0);

    dev->exit_eventloop = 0;
}

/*----------------------------------------------------------------------*\
* HandleEvents()
*
* Just a front-end to the update command.  
\*----------------------------------------------------------------------*/

static void
HandleEvents(PLStream *pls)
{
    dbug_enter("HandleEvents");

    tcl_cmd(pls, "$update_proc");
}

/*----------------------------------------------------------------------*\
* flush_output()
*
* Flushes output and sends command to the server to read from the 
* {FIFO|socket}.  The server processes the commands asynchronously since
* the "readdata" widget command is issued in the background.
*
* Some notes:
*
* When sending via a FIFO, we actually flush the FIFO _after_ the read
* command is issued to the renderer.  This way bigger buffers can be used
* since both processes can work on it asnychronously for a while.  If the
* renderer just hangs at some point, it may indicate the buffer is too
* large for your system and further writes aren't being permitted without
* first reading from the pipe.  And since the data writer is responsible
* for telling the server to do the reads, they both hang forever!  The
* only way to get around this type of problem is to use i/o events in the
* server's TK event loop.  (Actually there is another way: use
* non-blocking i/o with good error recovery, but that's a lot of work!)
* Maybe once the stock TK has this capability I will switch over, but
* sticking the input events in the TK event loop may be bad for
* performance.
*
* The socket i/o routines are modified versions of the ones from the
* Tcl-DP package.  They have been altered to take a pointer to a PDFstrm
* struct, and read-to or write-from pdfs->buffer.  The length of the
* buffer is stored in pdfs->bp (the original Tcl-DP routine assume the
* message is character data and use strlen).
\*----------------------------------------------------------------------*/

static void
flush_output(PLStream *pls)
{
    TkDev *dev = (TkDev *) pls->dev;
    PDFstrm *pdfs = (PDFstrm *) pls->pdfs;

    dbug_enter("flush_output");

#ifdef DEBUG
    fprintf(stderr, "%s: Flushing buffer, bytecnt = %d\n",
	    __FILE__, pls->bytecnt);
#endif

    tcl_cmd(pls, "$update_proc");

/* Send packet -- filehandler will be invoked automatically. */

    if (pl_PacketSend(dev->interp, dev->iodev, pls->pdfs)) {
	fprintf(stderr, "Packet send failed:\n\t %s\n",
		dev->interp->result);
	abort_session(pls, "");
    }
    pdfs->bp = 0;
    pls->bytecnt = 0;
}

/*----------------------------------------------------------------------*\
* Abort
*
* Just a TCL front-end to abort_session().
\*----------------------------------------------------------------------*/

static int
Abort(ClientData clientData, Tcl_Interp *interp, int argc, char **argv)
{
    PLStream *pls = (PLStream *) clientData;

    dbug_enter("Abort");

    abort_session(pls, "");
    return TCL_OK;
}

/*----------------------------------------------------------------------*\
* KeyEH()
*
* This TCL command handles keyboard events.
*
* Arguments:
*	command name
*	keysym name (textual string)
*	keysym value
*	ASCII equivalent (optional)
*
* The first argument is keysym name -- this is all that's really required 
* although it's better to send the numeric keysym value since then we
* can avoid a long lookup procedure.  Sometimes, when faking input, it
* is inconvenient to have to worry about what the numeric keysym value
* is, so in a few cases a missing keysym value is tolerated.
\*----------------------------------------------------------------------*/

static int
KeyEH(ClientData clientData, Tcl_Interp *interp, int argc, char **argv)
{
    PLStream *pls = (PLStream *) clientData;
    TkDev *dev = (TkDev *) pls->dev;

    PLKey key;
    char *keysym, c;
    int advance = 0;

    dbug_enter("KeyEH");

    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
		argv[0], " keysym-name ?keysym-value?\"", (char *) NULL);
	return TCL_ERROR;
    }
    key.code = 0;
    key.string[0] = '\0';

/* Keysym name */

    keysym = argv[1];

/* Keysym value */
/* If missing, explicitly check for a few common ones */

    if (argc > 2)
	key.code = atol(argv[2]);

    if (argc == 2 || key.code == 0) {
	c = *keysym;
	if ((c == 'B') && (strcmp(keysym, "BackSpace") == 0)) {
	    key.code = PLK_BackSpace;
	}
	else if ((c == 'D') && (strcmp(keysym, "Delete") == 0)) {
	    key.code = PLK_Delete;
	}
	else if ((c == 'L') && (strcmp(keysym, "Linefeed") == 0)) {
	    key.code = PLK_Linefeed;
	}
	else if ((c == 'R') && (strcmp(keysym, "Return") == 0)) {
	    key.code = PLK_Return;
	}
	else if ((c == 'P') && (strcmp(keysym, "Prior") == 0)) {
	    key.code = PLK_Prior;
	}
	else if ((c == 'N') && (strcmp(keysym, "Next") == 0)) {
	    key.code = PLK_Next;
	}
	else {
	    Tcl_AppendResult(interp, "Unrecognized keysym \"",
		    argv[1], "\"; must specify keycode", (char *) NULL);
	    return TCL_ERROR;
	}
    }

/* ASCII value */

    if (argc > 3) {
	key.string[0] = argv[3][0];
	key.string[1] = '\0';
    }

#ifdef DEBUG
    fprintf(stderr, "KeyEH: Keysym %s, hex %x, ASCII: %s\n",
	    keysym, key.code, key.string);
#endif

/* Call user event handler */
/* Since this is called first, the user can disable all plplot internal
   event handling by setting key.code to 0 and key.string to '\0' */

    if (pls->KeyEH != NULL)
	(*pls->KeyEH) (&key, pls->KeyEH_data, &advance);

/* Handle internal events */

/* Advance to next page (i.e. terminate event loop) on a <eol> */
/* Check for both <CR> and <LF> for portability, also a <Page Down> */

    if (key.code == PLK_Return ||
	key.code == PLK_Linefeed ||
	key.code == PLK_Next)
	advance = TRUE;

    if (advance) 
	dev->exit_eventloop = 1;

/* Terminate on a 'Q' (not 'q', since it's too easy to hit by mistake) */

    if (key.string[0] == 'Q') 
	tcl_cmd(pls, "abort");

    return TCL_OK;
}

/*----------------------------------------------------------------------*\
* tk_wait()
*
* Waits for the specified expression to evaluate to true before
* proceeding.  While we are waiting to proceed, all events (for this
* or other interpreters) are handled.  
*
* Use a static string buffer to hold the command, to ensure it's in
* writable memory (grrr...).
\*----------------------------------------------------------------------*/

static void
tk_wait(PLStream *pls, char *cmd)
{
    TkDev *dev = (TkDev *) pls->dev;
    int result = 0;

    dbug_enter("tk_wait");

    copybuf(pls, cmd);
    for (;;) {
	if (Tcl_ExprBoolean(dev->interp, dev->cmdbuf, &result)) {
	    fprintf(stderr, "tk_wait command \"%s\" failed:\n\t %s\n",
		    cmd, dev->interp->result);
	    break;
	}
	if (result)
	    break;

	Tk_DoOneEvent(0);
    }
}

/*----------------------------------------------------------------------*\
* server_cmd
*
* Sends specified command to server, aborting on an error.
* If nowait is set, the command is issued in the background.
*
* If commands MUST proceed in a certain order (e.g. initialization), it
* is safest to NOT run them in the background.
*
* In order to protect args that have embedded spaces in them, I enclose
* the entire command in a [list ...], but for TK sends ONLY.  If done with
* Tcl-DP RPC, the sent command is no longer recognized.  Evidently an
* extra scan of the line is done with TK sends for some reason.
\*----------------------------------------------------------------------*/

static void
server_cmd(PLStream *pls, char *cmd, int nowait)
{
    TkDev *dev = (TkDev *) pls->dev;
    static char dpsend_cmd0[] = "dp_RPC $server ";
    static char dpsend_cmd1[] = "dp_RDO $server ";
    static char tksend_cmd0[] = "send $server ";
    static char tksend_cmd1[] = "send $server after 1 ";
    int result;

    dbug_enter("server_cmd");
#ifdef DEBUG
    fprintf(stderr, "Sending command: %s\n", cmd);
#endif

    copybuf(pls, cmd);
    if (pls->dp) {
	if (nowait) 
	    result = Tcl_VarEval(dev->interp, dpsend_cmd1, dev->cmdbuf,
				 (char **) NULL);
	else
	    result = Tcl_VarEval(dev->interp, dpsend_cmd0, dev->cmdbuf,
				 (char **) NULL);
    } 
    else {
	if (nowait) 
	    result = Tcl_VarEval(dev->interp, tksend_cmd1, "[list ",
				 dev->cmdbuf, "]", (char **) NULL);
	else
	    result = Tcl_VarEval(dev->interp, tksend_cmd0, "[list ",
				 dev->cmdbuf, "]", (char **) NULL);
    }

    if (result) {
	fprintf(stderr, "Server command \"%s\" failed:\n\t %s\n",
		cmd, dev->interp->result);
	abort_session(pls, "");
    }
}

/*----------------------------------------------------------------------*\
* tcl_cmd
*
* Evals the specified command, aborting on an error.
\*----------------------------------------------------------------------*/

static void
tcl_cmd(PLStream *pls, char *cmd)
{
    TkDev *dev = (TkDev *) pls->dev;

    dbug_enter("tcl_cmd");
#ifdef DEBUG_ENTER
    fprintf(stderr, "Evaluating command: %s\n", cmd);
#endif

    if (tcl_eval(pls, cmd)) {
	fprintf(stderr, "TCL command \"%s\" failed:\n\t %s\n",
		cmd, dev->interp->result);
	abort_session(pls, "");
    }
}

/*----------------------------------------------------------------------*\
* tcl_eval
*
* Evals the specified string, returning the result.
\*----------------------------------------------------------------------*/

static int
tcl_eval(PLStream *pls, char *cmd)
{
    TkDev *dev = (TkDev *) pls->dev;

    copybuf(pls, cmd);
    return(Tcl_VarEval(dev->interp, dev->cmdbuf, (char **) NULL));
}

/*----------------------------------------------------------------------*\
* copybuf
*
* Puts command in a static string buffer, to ensure it's in writable
* memory (grrr...).
\*----------------------------------------------------------------------*/

static void
copybuf(PLStream *pls, char *cmd)
{
    TkDev *dev = (TkDev *) pls->dev;

    if (dev->cmdbuf == NULL) {
	dev->cmdbuf_len = 100;
	dev->cmdbuf = (char *) malloc(dev->cmdbuf_len);
    }

    if (strlen(cmd) >= dev->cmdbuf_len) {
	free((void *) dev->cmdbuf);
	dev->cmdbuf_len = strlen(cmd) + 20;
	dev->cmdbuf = (char *) malloc(dev->cmdbuf_len);
    }

    strcpy(dev->cmdbuf, cmd);
}

/*----------------------------------------------------------------------*/
#else
int
pldummy_tk()
{
    return 0;
}

#endif				/* TK */
