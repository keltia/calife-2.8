/** Fichier db.c
 **
 ** Routines de gestion des mots de passe et des fichiers
 ** de configuration pour calife.
 **
 ** Copyright (c) 1991-1995 by Ollivier ROBERT
 ** A distribuer selon les regles de la GNU GPL General Public Licence
 ** Voir le fichier COPYING fourni.
 **
 **/

#include "config.h"     /* GNU configure */

                        /* fichier de configuration */
#include "conf.h"

#ifndef lint
static char * rcsid = "$Id: //depot/security/calife/main/db.c#25 $";
#endif /* lint */

/** On cherche et ouvre la database locale.
 **
 ** Dans tous les cas, le fichier log est ouvert (il est local). Si syslogd(8)
 ** est actif on ouvre le "fichier" pour syslog(3). Le fichier de log devient
 ** maintenant dependant de NO_SYSLOG: s'il n'y a pas de syslog d'utilisable
 ** alors on ouvre le fichier de log.
 **
 ** Parametres :    neant
 **
 ** Retourne :      int     s'il y a un probleme renvoie 1 sinon 0.
 **/

#ifdef STDC_HEADERS
int    
open_databases (void)
#else /* !STDC_HEADERS */
int    
open_databases ()
#endif
{
#ifdef DEBUG
    fprintf (stderr, "Opening databases...\n");
    fprintf (stderr, "%s\n", rcsid);
    fflush (stderr);    
#endif
    /*
     * become root again
     */
    GET_ROOT;

    if (access (AUTH_CONFIG_FILE, 0))
    {
#ifndef NO_SYSLOG
        syslog (LOG_AUTH | LOG_ERR, "No database in `%s', launching su...\n", AUTH_CONFIG_FILE);
#endif /* NO_SYSLOG */
        return 1;        
    }
    else
    {
        /*
         * open log file
         */
#ifdef NO_SYSLOG
        if (!(logfile = fopen (ADMIN_LOG, "a+")))
            fprintf (stderr, "Can't open file `%s' errno=`%d'", ADMIN_LOG, errno);
        else
            fchmod (fileno (logfile), 0600);
#endif /* NO_SYSLOG */
                                /* open protected database */
        fp = fopen (AUTH_CONFIG_FILE, "r");
        if (fp == NULL)
            die (6, "calife: internal error, fp == NULL at %d\n", __LINE__);
        fchmod (fileno (fp), 0400);
    }
    /*
     * stay non root for a time
     */
    RELEASE_ROOT;

    return 0;
}

/** Verifie si l'utilisateur de nom name a le droit d'utiliser
 ** calife.
 **
 ** Line format is :
 **
 ** login[:][shell][:user1,user2,user3,...]
 **
 ** Parametres :    name        char *  nom de l'utilisateur
 **                 user_to_be  char *  futur nom d'utilisateur
 **
 ** Retourne :      int     1 si l'utilisateur est autorise 0 sinon.
 **/

#ifdef STDC_HEADERS
int 
verify_auth_info (char * name, char * user_to_be)
#else /* !STDC_HEADERS */
int 
verify_auth_info (name, user_to_be)
    char    * name;
    char    * user_to_be;
#endif
{
    int     allowed = 0, do_tok = 1;
    char    * * user_list = NULL;
    size_t  l_line = 0;
    int     nb_users = 0;
    char    * line, * ptr;
    char    * line_name, * line_shell;
    
    /*
     * let's search if user allowed or not
     */
#ifdef HAVE_WORKING_SYSCONF
    l_line = (size_t) sysconf (_SC_LINE_MAX);   /* returns long usually */
#else
    l_line = MAX_STRING;
#endif /* HAVE_WORKING_SYSCONF */    

    line = (char *) xalloc (l_line);
    while (!feof (fp))
    {
        int lastc;

        if (fgets (line, l_line, fp) == NULL)
            break;
        /*
         * remove trailing '\n' if present
         */
        lastc = strlen (line) - 1;
        if (line [lastc] == '\n')
            line [lastc] = '\0';
        /*
         * empty line or comment  or null login -- not allowed but ignored
         */
        if (!line || (*line) == '#' || (*line) == '\0')
            continue;
        MESSAGE_1 ("Line read = |%s|\n", line);
        line_name = line;
        /*
         * Look for first ':'
         */
        ptr = strchr (line, ':');
        if (ptr == NULL)
        {
            /*
             * No shell and no user list
             */
            custom_shell = 0;
            if (strcmp (line_name, name))   /* not us */
                continue;
            goto escape;            
        }
        /*
         * I got a ':', put a '\0' instead.
         */
        *ptr++ = '\0';
        /*
         * Do we have anything behind ?
         */
        if (*ptr == '\0')
            custom_shell = 0;

        MESSAGE_1 ("Current line_name = |%s|\n", line_name);

        if (strcmp (line_name, name))   /* not us */
            continue;

        line_shell = ptr;
        /*
         * Look for another ':', maybe just after the first one
         */
        ptr = strchr (line_shell, ':');
        if (ptr == line_shell)
            custom_shell = 0;            
        /*
         * If we got another one, put a '\0' instead
         */
        if (ptr)
            *ptr++ = '\0';
        /*
         * Analyze the shell candidate
         */
        if ((*(line_shell) == '*'))   /* LOCKED ACCOUNT */
        {
            allowed = 0;
            goto end;
        }
        /*
         * look for pathname
         */
        if ((*(line_shell) == '/'))
        {
            /*
             * Is this a real shell ?
             */
            if (access (line_shell, R_OK | X_OK))
            {
                MESSAGE_1 (" access of |%s| failed, no custom_shell\n",
                           line_shell);
                custom_shell = 0;
            }
            else
            {
                custom_shell = 1;
                shell = (char *) xalloc (strlen (line_shell) + 1);
                strncpy (shell, line_shell, strlen (line_shell));
                shell [strlen (line_shell)] = '\0';
                MESSAGE_1 (" current line_shell = |%s|\n", line_shell);
            }
        }
        else
        {
            custom_shell = 0;
            do_tok = 0;
        }
        MESSAGE_1 (" custom_shell = |%d|\n", custom_shell);

        /*
         * Analyze the rest of the line
         */
        if (ptr)
        {
            /*
             * we've got a user list
             */
            if (*ptr)
            {
                char  * p, ** q;
                
                MESSAGE_1 (" current user_list = |%s|\n", ptr);
                /*
                 * the list should be user1,user2,...,user_n
                 */
                p = ptr;     /* count users # in the list */
                while (p && *p)
                    if (*p++ == ',')
                        nb_users++;
                if (*--p != ',')    /* last char is not a ',' */
                    nb_users++;

                MESSAGE_1 ("  users # = |%d|\n", nb_users);

                /*
                 * two passes to avoid fixed size allocations
                 */ 
                user_list = (char **) calloc (nb_users + 1, sizeof (char *));
                if (user_list == NULL)
                    die (255, "Out of memory at line %d in file %s\n",
                         __LINE__, __FILE__);
                /*
                 * put pointers in user_list
                 */
                p = ptr;     
                q = user_list;
                /*
                 * 1st user is immediate
                 */
                (*q++) = p;
                while (p && *p)
                {
                    if (*p == ',')    /* found a ',' */
                    {
                        *p = '\0';
                        if (*++p == ',')
                            continue;
                        (*q++) = p++;     /* next char is user name */
                    }            
                    else
                        p++;        /* only regular letters */
                }            
            }
        }        
        /*
         * user_list is complete, now compare
         */
escape:
        if (user_list)
        {
            int i;
            
            for ( i = 0; i < nb_users; i++ )
            {
                MESSAGE_2(" comparing |%s| to |%s|\n", 
                          user_to_be, user_list [i]);
                if (!strcmp (user_list [i], user_to_be))
                {
                    allowed = 1;
                    nb_users = 0;
                    goto end;   /* beuh but too many loops */
                }
            }            
        }
        else
        {
            allowed = 1;
            nb_users = 0;
            break;            
        }
    }
end:
    free (line);
    if (user_list)
        free (user_list);
    fclose (fp);
    return allowed;    
}

/** Demande et verifie le mot de passe pour l'utilisateur name
 ** Si name == root on ne fait rien.
 **
 ** Parametres :    name        char *  nom de l'utilisateur
 **                 user_to_be  char *  qui va-t-on devenir
 **                 this_time   char *  quand ?
 **                 tty         char *  sur quel tty
 **
 ** Retourne :      neant
 **/

#ifdef STDC_HEADERS
void    
verify_password (char * name, char * user_to_be, char * this_time, char * tty)
#else /* !STDC_HEADERS */
void    
verify_password (name, user_to_be, this_time, tty)
    char    * name, * user_to_be, * this_time, * tty;
#endif /* STDC_HEADERS */
{
    int             i = 0;
    size_t          l_size = 0;
    
#if defined (HAVE_SHADOW_H) && defined (HAVE_GETSPNAM) && !defined(UNUSED_SHADOW)
    struct  spwd  * scalife;
#endif
    struct  passwd  * calife;

     /*
      * become root again
      */
    GET_ROOT;

    /*
     * returns long usually
     */
#ifdef HAVE_WORKING_SYSCONF
    l_size = (size_t) sysconf (_SC_LINE_MAX);
#else
    l_size = MAX_STRING;
#endif /* HAVE_WORKING_SYSCONF */    

    calife = getpwnam (name);       /* null or locked password */
    if (calife == NULL)
        die (1, "Bad pw data at line %d", __LINE__);
    
#if defined (HAVE_SHADOW_H) && defined (HAVE_GETSPNAM) && !defined(UNUSED_SHADOW)
    scalife = getspnam (name);       /* null or locked password */
    if (scalife)
    {
        calife->pw_passwd = (char *) xalloc (strlen (scalife->sp_pwdp) + 1);
        strcpy (calife->pw_passwd, scalife->sp_pwdp);
    }
#endif /* HAVE_SHADOW_H */
    if ((*(calife->pw_passwd)) == '\0' || (*(calife->pw_passwd)) == '*')
    {

#ifndef NO_SYSLOG
        syslog (LOG_AUTH | LOG_ERR, "NULL CALIFE %s to %s on %s", name, user_to_be, tty);
        closelog ();
#else /* NO_SYSLOG */        
        if (logfile)
        {
            fprintf (logfile, "USER=%-9sTTY=%s\tDATE=%s NAME=%s << NULL PASSWORD >>\n",
                name, tty, this_time, user_to_be);
            fflush (logfile);
            fclose (logfile);
        }
#endif /* NO_SYSLOG */
        die (10, "Sorry.\n");
    }
#ifndef RELAXED
    if (getuid () != 0)
    {
        char    got_pass = 0;
        char    * pt_pass, * pt_enc, 
                * user_pass, * enc_pass, salt [10];

        user_pass = (char *) xalloc (l_size);
        enc_pass = (char *) xalloc (l_size);
        
        /*
         * cope with MD5 based crypt(3)
         */
        if (!strncmp (calife->pw_passwd, "$1$", 3)) /* MD5 */
        {
            char * pp = (char *) xalloc (strlen (calife->pw_passwd) + 1);
            char * md5_salt;
            char * md5_pass;
            
            strcpy (pp, calife->pw_passwd + 3);
            md5_salt = strtok (pp, "$");
            md5_pass = strtok (NULL, "$");
            
            if (md5_pass == NULL || 
                md5_salt == NULL ||
                (strlen (md5_salt) > 8))   /* garbled password */
            {
                syslog (LOG_AUTH | LOG_ERR, "GARBLED PASSWORD %s to unknown %s on %s", name, user_to_be, tty);
                die (8, "Bad password string.\n");
            }
            MESSAGE_1 ("MD5 password found, salt=%s\n", md5_salt);
            strcpy (salt, md5_salt);            
            free (pp);
        }
        else
        {       
            strncpy (salt, calife->pw_passwd, 2);
            salt [2] = '\0';
        }

        for ( i = 0; i < 3; i ++ )
        {
            pt_pass = (char *) getpass ("Password:");
            memset (user_pass, '\0', l_size);
            strcpy (user_pass, pt_pass);
            pt_enc = (char *) crypt (user_pass, calife->pw_passwd);
            memset (enc_pass, '\0', l_size);
            strcpy (enc_pass, pt_enc);
            /*
             * assumes standard crypt(3)
             */
            if (!strcmp (enc_pass, calife->pw_passwd))
            {
                got_pass = 1;
                break;
            }
        }

        if (!got_pass)
        {
#ifndef NO_SYSLOG
            syslog (LOG_AUTH | LOG_ERR, "BAD CALIFE %s to %s on %s", name, user_to_be, tty);
            closelog ();
#else /* NO_SYSLOG */
            if (logfile)
            {
                fprintf (logfile, "USER=%-9sTTY=%s\tDATE=%s NAME=%s << BAD PASSWORD >>\n",
                         name, tty, this_time, user_to_be);
                fflush (logfile);
                fclose (logfile);
            }
#endif /* NO_SYSLOG */
            fprintf (stderr, "Sorry.\n");
            exit (9);
        }
        free (user_pass);
        free (enc_pass);
    }    
#endif /* RELAXED */
    /*
     * stay non root for a time
     */
    RELEASE_ROOT;
}


