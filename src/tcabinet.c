/**
 * tcabinet.c -- Tokyo Cabinet database functions
 * Copyright (C) 2009-2014 by Gerardo Orellana <goaccess@prosoftcorp.com>
 * GoAccess - An Ncurses apache weblog analyzer & interactive viewer
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * A copy of the GNU General Public License is attached to this
 * source distribution for its full text.
 *
 * Visit http://goaccess.prosoftcorp.com for new releases.
 */

#include <errno.h>
#include <tcutil.h>
#include <tcbdb.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "tcabinet.h"

#ifdef HAVE_LIBGEOIP
#include "geolocation.h"
#endif

#include "browsers.h"
#include "commons.h"
#include "error.h"
#include "opesys.h"
#include "parser.h"
#include "settings.h"
#include "sort.h"
#include "util.h"
#include "xmalloc.h"

#ifdef TCB_BTREE

TCBDB *ht_browsers = NULL;
TCBDB *ht_countries = NULL;
TCBDB *ht_date_bw = NULL;
TCBDB *ht_file_bw = NULL;
TCBDB *ht_file_serve_usecs = NULL;
TCBDB *ht_general_stats = NULL;
TCBDB *ht_host_bw = NULL;
TCBDB *ht_hostnames = NULL;
TCBDB *ht_hosts_agents = NULL;
TCBDB *ht_host_serve_usecs = NULL;
TCBDB *ht_hosts = NULL;
TCBDB *ht_keyphrases = NULL;
TCBDB *ht_not_found_requests = NULL;
TCBDB *ht_os = NULL;
TCBDB *ht_referrers = NULL;
TCBDB *ht_referring_sites = NULL;
TCBDB *ht_request_keys = NULL;
TCBDB *ht_request_methods = NULL;
TCBDB *ht_request_protocols = NULL;
TCBDB *ht_requests = NULL;
TCBDB *ht_requests_static = NULL;
TCBDB *ht_status_code = NULL;
TCBDB *ht_unique_visitors = NULL;
TCBDB *ht_unique_vis = NULL;

#else

TCMDB *ht_browsers = NULL;
TCMDB *ht_countries = NULL;
TCMDB *ht_date_bw = NULL;
TCMDB *ht_file_bw = NULL;
TCMDB *ht_file_serve_usecs = NULL;
TCMDB *ht_host_bw = NULL;
TCMDB *ht_hostnames = NULL;
TCMDB *ht_hosts = NULL;
TCMDB *ht_host_serve_usecs = NULL;
TCMDB *ht_keyphrases = NULL;
TCMDB *ht_not_found_requests = NULL;
TCMDB *ht_os = NULL;
TCMDB *ht_hosts_agents = NULL;
TCMDB *ht_referrers = NULL;
TCMDB *ht_referring_sites = NULL;
TCMDB *ht_request_methods = NULL;
TCMDB *ht_requests = NULL;
TCMDB *ht_request_keys = NULL;
TCMDB *ht_request_protocols = NULL;
TCMDB *ht_requests_static = NULL;
TCMDB *ht_status_code = NULL;
TCMDB *ht_unique_vis = NULL;
TCMDB *ht_unique_visitors = NULL;

#endif

#ifdef TCB_BTREE
static char *
tc_db_set_path (const char *dbname)
{
  char *path;

  if (conf.db_path != NULL) {
    path = xmalloc (snprintf (NULL, 0, "%s%s", conf.db_path, dbname) + 1);
    sprintf (path, "%s%s", conf.db_path, dbname);
  } else {
    path = xmalloc (snprintf (NULL, 0, "%s%s", TC_DBPATH, dbname) + 1);
    sprintf (path, "%s%s", TC_DBPATH, dbname);
  }
  return path;
}
#endif

/* Open the database handle */
#ifdef TCB_BTREE
static TCBDB *
tc_db_create (const char *dbname)
{
  TCBDB *bdb;
  char *path = NULL;
  int ecode;
  uint32_t lcnum, ncnum, lmemb, nmemb, bnum, flags;

  path = tc_db_set_path (dbname);

  bdb = tcbdbnew ();

  lcnum = conf.cache_lcnum > 0 ? conf.cache_lcnum : TC_LCNUM;
  ncnum = conf.cache_ncnum > 0 ? conf.cache_ncnum : TC_NCNUM;

  LOG_DEBUG (("%s\n", path));
  LOG_DEBUG (("lcnum, ncnum: %d, %d\n", lcnum, ncnum));

  /* set the caching parameters of a B+ tree database object */
  if (!tcbdbsetcache (bdb, lcnum, ncnum)) {
    free (path);
    FATAL ("Unable to set TCB cache");
  }

  LOG_DEBUG (("xmmap: %d\n", conf.xmmap));
  /* set the size of the extra mapped memory */
  if (conf.xmmap > 0 && !tcbdbsetxmsiz (bdb, conf.xmmap)) {
    free (path);
    FATAL ("Unable to set TCB xmmap.");
  }

  lmemb = conf.tune_lmemb > 0 ? conf.tune_lmemb : TC_LMEMB;
  nmemb = conf.tune_nmemb > 0 ? conf.tune_nmemb : TC_NMEMB;
  bnum = conf.tune_bnum > 0 ? conf.tune_bnum : TC_BNUM;

  /* compression */
  flags = BDBTLARGE;
  LOG_DEBUG (("flags: BDBTLARGE"));
  if (conf.compression == TC_BZ2) {
    flags |= BDBTBZIP;
    LOG_DEBUG ((" | BDBTBZIP"));
  } else if (conf.compression == TC_ZLIB) {
    flags |= BDBTDEFLATE;
    LOG_DEBUG ((" | BDBTDEFLATE"));
  }

  LOG_DEBUG (("\nlmemb, nmemb, bnum: %d, %d, %d\n\n", lmemb, nmemb, bnum));
  /* set the tuning parameters */
  tcbdbtune (bdb, lmemb, nmemb, bnum, 8, 10, flags);

  /* open flags */
  flags = BDBOWRITER | BDBOCREAT;
  if (!conf.load_from_disk)
    flags |= BDBOTRUNC;

  /* attempt to open the database */
  if (!tcbdbopen (bdb, path, flags)) {
    free (path);
    ecode = tcbdbecode (bdb);

    FATAL ("%s", tcbdberrmsg (ecode));
  }
  free (path);

  return bdb;
}
#endif

#ifdef TCB_MEMHASH
static TCMDB *
tc_ht_create (void)
{
  TCMDB *mdb = tcmdbnew ();
  return mdb;
}
#endif

/* Initialize TokyoCabinet storage */
void
init_storage (void)
{
/* *INDENT-OFF* */
#ifdef TCB_BTREE
  ht_browsers           = tc_db_create (DB_BROWSERS);
  ht_countries          = tc_db_create (DB_COUNTRIES);
  ht_date_bw            = tc_db_create (DB_DATE_BW);
  ht_file_bw            = tc_db_create (DB_FILE_BW);
  ht_file_serve_usecs   = tc_db_create (DB_FILE_SERVE_USECS);
  ht_general_stats      = tc_db_create (DB_GENERAL_STATS);
  ht_host_bw            = tc_db_create (DB_HOST_BW);
  ht_hostnames          = tc_db_create (DB_HOSTNAMES);
  ht_hosts_agents       = tc_db_create (DB_HOST_AGENTS);
  ht_host_serve_usecs   = tc_db_create (DB_HOST_SERVE_USECS);
  ht_hosts              = tc_db_create (DB_HOSTS);
  ht_keyphrases         = tc_db_create (DB_KEYPHRASES);
  ht_not_found_requests = tc_db_create (DB_NOT_FOUND_REQUESTS);
  ht_os                 = tc_db_create (DB_OS);
  ht_referrers          = tc_db_create (DB_REFERRERS);
  ht_referring_sites    = tc_db_create (DB_REFERRING_SITES);
  ht_request_keys       = tc_db_create (DB_REQUEST_KEYS);
  ht_request_methods    = tc_db_create (DB_REQUEST_METHODS);
  ht_request_protocols  = tc_db_create (DB_REQUEST_PROTOCOLS);
  ht_requests_static    = tc_db_create (DB_REQUESTS_STATIC);
  ht_requests           = tc_db_create (DB_REQUESTS);
  ht_status_code        = tc_db_create (DB_STATUS_CODE);
  ht_unique_visitors    = tc_db_create (DB_UNIQUE_VISITORS);
  ht_unique_vis         = tc_db_create (DB_UNIQUE_VIS);
#else
  ht_browsers           = tc_ht_create ();
  ht_countries          = tc_ht_create ();
  ht_date_bw            = tc_ht_create ();
  ht_file_bw            = tc_ht_create ();
  ht_file_serve_usecs   = tc_ht_create ();
  ht_host_bw            = tc_ht_create ();
  ht_hostnames          = tc_ht_create ();
  ht_hosts_agents       = tc_ht_create ();
  ht_host_serve_usecs   = tc_ht_create ();
  ht_hosts              = tc_ht_create ();
  ht_keyphrases         = tc_ht_create ();
  ht_not_found_requests = tc_ht_create ();
  ht_os                 = tc_ht_create ();
  ht_referrers          = tc_ht_create ();
  ht_referring_sites    = tc_ht_create ();
  ht_request_methods    = tc_ht_create ();
  ht_request_protocols  = tc_ht_create ();
  ht_requests_static    = tc_ht_create ();
  ht_requests           = tc_ht_create ();
  ht_request_keys       = tc_ht_create ();
  ht_status_code        = tc_ht_create ();
  ht_unique_visitors    = tc_ht_create ();
  ht_unique_vis         = tc_ht_create ();
#endif
/* *INDENT-ON* */
}

void
free_storage (void)
{
#ifdef HAVE_LIBGEOIP
  tc_db_close (ht_countries, DB_COUNTRIES);
#endif
#ifdef TCB_BTREE
  tc_db_close (ht_general_stats, DB_GENERAL_STATS);
#endif
  tc_db_close (ht_browsers, DB_BROWSERS);
  tc_db_close (ht_date_bw, DB_DATE_BW);
  tc_db_close (ht_file_bw, DB_FILE_BW);
  tc_db_close (ht_file_serve_usecs, DB_FILE_SERVE_USECS);
  tc_db_close (ht_host_bw, DB_HOST_BW);
  tc_db_close (ht_hosts_agents, DB_HOST_AGENTS);
  tc_db_close (ht_hosts, DB_HOSTS);
  tc_db_close (ht_host_serve_usecs, DB_HOST_SERVE_USECS);
  tc_db_close (ht_keyphrases, DB_KEYPHRASES);
  tc_db_close (ht_not_found_requests, DB_NOT_FOUND_REQUESTS);
  tc_db_close (ht_os, DB_OS);
  tc_db_close (ht_referrers, DB_REFERRERS);
  tc_db_close (ht_referring_sites, DB_REFERRING_SITES);
  tc_db_close (ht_request_keys, DB_REQUEST_KEYS);
  tc_db_close (ht_request_methods, DB_REQUEST_METHODS);
  tc_db_close (ht_request_protocols, DB_REQUEST_PROTOCOLS);
  tc_db_close (ht_requests, DB_REQUESTS);
  tc_db_close (ht_requests_static, DB_REQUESTS_STATIC);
  tc_db_close (ht_status_code, DB_STATUS_CODE);
  tc_db_close (ht_unique_vis, DB_UNIQUE_VIS);
  tc_db_close (ht_unique_visitors, DB_UNIQUE_VISITORS);
}

/* Close the database handle */
#ifdef TCB_BTREE
int
tc_db_close (void *db, const char *dbname)
{
  TCBDB *bdb = db;
  char *path = NULL;
  int ecode;

  if (bdb == NULL)
    return 1;

  /* close the database */
  if (!tcbdbclose (bdb)) {
    ecode = tcbdbecode (bdb);
    FATAL ("%s", tcbdberrmsg (ecode));
  }
  /* delete the object */
  tcbdbdel (bdb);

  if (conf.keep_db_files || conf.load_from_disk)
    return 0;

  /* remove database file */
  path = tc_db_set_path (dbname);
  if (!tcremovelink (path))
    LOG_DEBUG (("Unable to remove DB: %s\n", path));
  free (path);

  return 0;
}
#endif

#ifdef TCB_MEMHASH
int
tc_db_close (void *db, GO_UNUSED const char *dbname)
{
  TCMDB *mdb = db;
  if (mdb == NULL)
    return 1;
  tcmdbdel (mdb);
  return 0;
}
#endif

#ifdef TCB_BTREE
/* Calls the given function for each of the key/value pairs */
void
tc_db_foreach (void *db, void (*fp) (BDBCUR * cur, char *k, int s, void *u),
               void *user_data)
{
  TCBDB *bdb = db;
  BDBCUR *cur;
  int ksize;
  char *key = NULL;

  cur = tcbdbcurnew (bdb);
  tcbdbcurfirst (cur);
  while ((key = tcbdbcurkey (cur, &ksize)) != NULL)
    (*fp) (cur, key, ksize, user_data);

  tcbdbcurdel (cur);
}
#endif

#ifdef TCB_MEMHASH
/* Calls the given function for each of the key/value pairs */
void
tc_db_foreach (void *db, void (*fp) (TCMDB * m, char *k, int s, void *u),
               void *user_data)
{
  TCMDB *mdb = db;
  int ksize;
  char *key = NULL;

  tcmdbiterinit (mdb);
  while ((key = tcmdbiternext (mdb, &ksize)) != NULL)
    (*fp) (mdb, key, ksize, user_data);
}
#endif

/* Return number of records of a hash database */
unsigned int
get_ht_size (void *db)
{
#ifdef TCB_BTREE
  TCBDB *bdb = db;
  if (bdb == NULL)
    return 0;
  return tcbdbrnum (bdb);
#else
  TCMDB *mdb = db;
  if (mdb == NULL)
    return 0;
  return tcmdbrnum (mdb);
#endif
}

/* Add an integer to a record */
static int
tc_db_add_int (void *db, const char *k, int n)
{
#ifdef TCB_BTREE
  TCBDB *bdb = db;
  return tcbdbaddint (bdb, k, strlen (k), n) == 1 ? KEY_NOT_FOUND : KEY_FOUND;
#else
  TCMDB *mdb = db;
  return tcmdbaddint (mdb, k, strlen (k), n) == 1 ? KEY_NOT_FOUND : KEY_FOUND;
#endif
}

/* Store generic data into the given hash table */
int
process_generic_data (void *db, const char *k)
{
  return tc_db_add_int (db, k, 1);
}

static void *
tc_db_get (void *db, const char *k)
{
  int sp = 0;
#ifdef TCB_BTREE
  TCBDB *bdb = db;
  return tcbdbget (bdb, k, strlen (k), &sp);
#else
  TCMDB *mdb = db;
  return tcmdbget (mdb, k, strlen (k), &sp);
#endif
}

static void
tc_db_put (void *db, const char *k, void *v, uint32_t v_size)
{
#ifdef TCB_BTREE
  int ecode;
  TCBDB *bdb = db;
  if (!tcbdbput (bdb, k, strlen (k), v, v_size)) {
    ecode = tcbdbecode (bdb);
    FATAL ("%s", tcbdberrmsg (ecode));
  }
#else
  TCMDB *mdb = db;
  tcmdbput (mdb, k, strlen (k), v, v_size);
#endif
}

void *
tc_db_get_str (void *db, const char *k)
{
#ifdef TCB_BTREE
  TCBDB *bdb = db;
  return tcbdbget2 (bdb, k);
#else
  TCMDB *mdb = db;
  return tcmdbget2 (mdb, k);
#endif
}

void
tc_db_put_str (void *db, const char *k, const char *v)
{
#ifdef TCB_BTREE
  int ecode;
  TCBDB *bdb = db;
  if (!tcbdbput2 (bdb, k, v)) {
    ecode = tcbdbecode (bdb);
    FATAL ("%s", tcbdberrmsg (ecode));
  }
#else
  TCMDB *mdb = db;
  tcmdbput2 (mdb, k, v);
#endif
}

int
tc_db_get_int (void *db, const char *k)
{
  void *value;
  int num = 0;

  if ((db == NULL) || (k == NULL))
    return (EINVAL);

  if ((value = tc_db_get (db, k)) != NULL) {
    num = (*(int *) value);
    free (value);
  }

  return num;
}

uint64_t
tc_db_get_uint64 (void *db, const char *k)
{
  uint64_t num = 0;
  void *value;

  if ((db == NULL) || (k == NULL))
    return (EINVAL);

  if ((value = tc_db_get (db, k)) != NULL) {
    num = (*(uint64_t *) value);
    free (value);
  }

  return num;
}

int
process_request (void *db, const char *k, const GLogItem * glog)
{
  if ((db == NULL) || (k == NULL))
    return (EINVAL);

  process_generic_data (db, k);
  if (conf.append_protocol && glog->protocol)
    tc_db_put_str (ht_request_protocols, k, glog->protocol);
  if (conf.append_method && glog->method)
    tc_db_put_str (ht_request_methods, k, glog->method);
  tc_db_put_str (ht_request_keys, k, glog->req);

  return 0;
}

int
process_request_meta (void *db, const char *k, uint64_t size)
{
  void *value;
  uint64_t add_value;

  if ((db == NULL) || (k == NULL))
    return (EINVAL);

  if ((value = tc_db_get (db, k)) != NULL) {
    add_value = (*(uint64_t *) value) + size;
  } else {
    add_value = 0 + size;
  }
  tc_db_put (db, k, &add_value, sizeof (uint64_t));
  free (value);

  return 0;
}

int
process_opesys (void *db, const char *k, const char *os_type)
{
  GOpeSys *opesys;

  if ((db == NULL) || (k == NULL))
    return (EINVAL);

  if ((opesys = tc_db_get (db, k)) != NULL) {
    opesys->hits++;
  } else {
    opesys = xcalloc (1, sizeof (GOpeSys));
    xstrncpy (opesys->os_type, os_type, OPESYS_TYPE_LEN);
    opesys->hits = 1;
  }
  tc_db_put (db, k, opesys, sizeof (GOpeSys));
  if (opesys)
    free (opesys);

  return 0;
}

int
process_browser (void *db, const char *k, const char *browser_type)
{
  GBrowser *browser;

  if ((db == NULL) || (k == NULL))
    return (EINVAL);

  if ((browser = tc_db_get (db, k)) != NULL) {
    browser->hits++;
  } else {
    browser = xcalloc (1, sizeof (GOpeSys));
    xstrncpy (browser->browser_type, browser_type, BROWSER_TYPE_LEN);
    browser->hits = 1;
  }
  tc_db_put (db, k, browser, sizeof (GBrowser));
  if (browser)
    free (browser);

  return 0;
}

#ifdef HAVE_LIBGEOIP
int
process_geolocation (void *db, const char *ctry, const char *cont,
                     const char *city)
{
  GLocation *location;

  if ((db == NULL) || (ctry == NULL))
    return (EINVAL);

  if ((location = tc_db_get (db, ctry)) != NULL) {
    location->hits++;
  } else {
    location = xcalloc (1, sizeof (GLocation));
    xstrncpy (location->continent, cont, CONTINENT_LEN);
    if (city[0] != '\0')
      xstrncpy (location->city, city, CITY_LEN);
    location->hits = 1;
  }
  tc_db_put (db, ctry, location, sizeof (GLocation));
  if (location)
    free (location);

  return 0;
}
#endif

/* process host agent strings */
int
process_host_agents (char *host, char *agent)
{
#ifdef TCB_BTREE
  TCBDB *db = ht_hosts_agents;
#else
  TCMDB *db = ht_hosts_agents;
#endif

  char *ptr_value = NULL, *tmp = NULL, *a = NULL;
  void *value_ptr;
  size_t len1, len2;

  if ((db == NULL) || (host == NULL) || (agent == NULL))
    return (EINVAL);

  a = xstrdup (agent);

  if ((value_ptr = tc_db_get_str (db, host)) != NULL) {
    ptr_value = (char *) value_ptr;
    if (strstr (ptr_value, a)) {
      if (a != NULL)
        goto out;
    }

    len1 = strlen (ptr_value);
    len2 = strlen (a);

    tmp = xmalloc (len1 + len2 + 2);
    memcpy (tmp, ptr_value, len1);
    tmp[len1] = '|';
    /* NUL-terminated */
    memcpy (tmp + len1 + 1, a, len2 + 1);
  } else
    tmp = alloc_string (a);

  tc_db_put_str (db, host, tmp);

out:
  if (a != NULL)
    free (a);
  if (tmp != NULL)
    free (tmp);
  if (value_ptr != NULL)
    free (value_ptr);

  return 0;
}

uint64_t
get_serve_time (const char *k, GModule module)
{
#ifdef TCB_BTREE
  TCBDB *db = NULL;
#else
  TCMDB *db = NULL;
#endif

  uint64_t serve_time = 0;
  void *value;

  /* serve time modules */
  switch (module) {
  case HOSTS:
    db = ht_host_serve_usecs;
    break;
  case REQUESTS:
  case REQUESTS_STATIC:
  case NOT_FOUND:
    db = ht_file_serve_usecs;
    break;
  default:
    db = NULL;
  }

  if (db == NULL)
    return 0;

  if ((value = tc_db_get (db, k)) != NULL) {
    serve_time = (*(uint64_t *) value);
    free (value);
  }

  return serve_time;
}

uint64_t
get_bandwidth (char *k, GModule module)
{
#ifdef TCB_BTREE
  TCBDB *db = NULL;
#else
  TCMDB *db = NULL;
#endif
  uint64_t bw = 0;
  void *value;

  /* bandwidth modules */
  switch (module) {
  case VISITORS:
    db = ht_date_bw;
    break;
  case REQUESTS:
  case REQUESTS_STATIC:
  case NOT_FOUND:
    db = ht_file_bw;
    break;
  case HOSTS:
    db = ht_host_bw;
    break;
  default:
    db = NULL;
  }

  if (db == NULL)
    return 0;

  if ((value = tc_db_get (db, k)) != NULL) {
    bw = (*(uint64_t *) value);
    free (value);
  }

  return bw;
}

char *
get_request_meta (const char *k, GReqMeta meta_req)
{
#ifdef TCB_BTREE
  TCBDB *db = NULL;
#else
  TCMDB *db = NULL;
#endif
  void *value;

  switch (meta_req) {
  case REQUEST:
    db = ht_request_keys;
    break;
  case REQUEST_METHOD:
    db = ht_request_methods;
    break;
  case REQUEST_PROTOCOL:
    db = ht_request_protocols;
    break;
  default:
    db = NULL;
  }

  if (db == NULL)
    return 0;

  if ((value = tc_db_get_str (db, k)) != NULL)
    return (char *) value;

  return alloc_string ("---");
}

#ifdef TCB_BTREE
TCBDB *
get_ht_by_module (GModule module)
{
  TCBDB *bdb;

  switch (module) {
  case VISITORS:
    bdb = ht_unique_vis;
    break;
  case REQUESTS:
    bdb = ht_requests;
    break;
  case REQUESTS_STATIC:
    bdb = ht_requests_static;
    break;
  case NOT_FOUND:
    bdb = ht_not_found_requests;
    break;
  case HOSTS:
    bdb = ht_hosts;
    break;
  case OS:
    bdb = ht_os;
    break;
  case BROWSERS:
    bdb = ht_browsers;
    break;
  case REFERRERS:
    bdb = ht_referrers;
    break;
  case REFERRING_SITES:
    bdb = ht_referring_sites;
    break;
  case KEYPHRASES:
    bdb = ht_keyphrases;
    break;
#ifdef HAVE_LIBGEOIP
  case GEO_LOCATION:
    bdb = ht_countries;
    break;
#endif
  case STATUS_CODES:
    bdb = ht_status_code;
    break;
  default:
    return NULL;
  }

  return bdb;
}
#endif

#ifdef TCB_MEMHASH
TCMDB *
get_ht_by_module (GModule module)
{
  TCMDB *mdb;

  switch (module) {
  case VISITORS:
    mdb = ht_unique_vis;
    break;
  case REQUESTS:
    mdb = ht_requests;
    break;
  case REQUESTS_STATIC:
    mdb = ht_requests_static;
    break;
  case NOT_FOUND:
    mdb = ht_not_found_requests;
    break;
  case HOSTS:
    mdb = ht_hosts;
    break;
  case OS:
    mdb = ht_os;
    break;
  case BROWSERS:
    mdb = ht_browsers;
    break;
  case REFERRERS:
    mdb = ht_referrers;
    break;
  case REFERRING_SITES:
    mdb = ht_referring_sites;
    break;
  case KEYPHRASES:
    mdb = ht_keyphrases;
    break;
#ifdef HAVE_LIBGEOIP
  case GEO_LOCATION:
    mdb = ht_countries;
    break;
#endif
  case STATUS_CODES:
    mdb = ht_status_code;
    break;
  default:
    return NULL;
  }

  return mdb;
}
#endif

static void
set_raw_data (char *key, void *value, GRawData * raw_data)
{
  raw_data->items[raw_data->idx].key = key;
  raw_data->items[raw_data->idx].value = value;
  raw_data->idx++;
}

#ifdef TCB_BTREE
static void
data_iter_generic (BDBCUR * cur, char *key, GO_UNUSED int ksize,
                   void *user_data)
{
  GRawData *raw_data = user_data;
  void *value;
  int vsize = 0;

  value = tcbdbcurval (cur, &vsize);
  if (value)
    set_raw_data (key, value, raw_data);
  tcbdbcurnext (cur);
}
#endif

#ifdef TCB_MEMHASH
static void
data_iter_generic (TCMDB * mdb, char *key, GO_UNUSED int ksize, void *user_data)
{
  GRawData *raw_data = user_data;
  void *value;

  value = tc_db_get (mdb, key);
  if (value)
    set_raw_data (key, value, raw_data);
}
#endif

GRawData *
parse_raw_data (void *db, int ht_size, GModule module)
{
  GRawData *raw_data;

  raw_data = new_grawdata ();
  raw_data->size = ht_size;
  raw_data->module = module;
  raw_data->idx = 0;
  raw_data->items = new_grawdata_item (ht_size);

  tc_db_foreach (db, data_iter_generic, raw_data);
  sort_raw_data (raw_data, module, ht_size);

  return raw_data;
}
