/*
 * SPDX-FileCopyrightText: 2014 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 * SPDX-FileCopyrightText: 2014 David Goulet <dgoulet@efficios.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#ifndef LTTNG_LOAD_H
#define LTTNG_LOAD_H

#include <lttng/lttng-export.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The lttng_load_session_attr object is opaque to the user. Use the helper
 * functions below to use it.
 */
struct lttng_load_session_attr;

/*
 * Return a newly allocated load session attribute object or NULL on error.
 */
LTTNG_EXPORT extern struct lttng_load_session_attr *lttng_load_session_attr_create(void);

/*
 * Free a given load session attribute object.
 */
LTTNG_EXPORT extern void lttng_load_session_attr_destroy(struct lttng_load_session_attr *attr);

/*
 * Load session attribute getter family of functions.
 */

/* Return session name. NULL indicates all sessions must be loaded. */
LTTNG_EXPORT extern const char *
lttng_load_session_attr_get_session_name(struct lttng_load_session_attr *attr);
/*
 * Return input URL. A NULL value indicates the default session
 * configuration location. The URL format used is documented in lttng-create(1).
 * NULL indicates that the default session configuration path is used.
 */
LTTNG_EXPORT extern const char *
lttng_load_session_attr_get_input_url(struct lttng_load_session_attr *attr);

/*
 * Return the configuration overwrite attribute. This attribute indicates
 * whether or not the loaded session should be loaded even if a session with the
 * same name already exists. If such a session exists, it is destroyed before
 * the replacement is loaded.
 */
LTTNG_EXPORT extern int lttng_load_session_attr_get_overwrite(struct lttng_load_session_attr *attr);

/*
 * Return the destination URL configuration override attribute. This attribute
 * indicates a destination URL override to be applied during the loading of the
 * configuration.
 *
 * NULL indicates no override will be applied on configuration load.
 */
LTTNG_EXPORT extern const char *
lttng_load_session_attr_get_override_url(struct lttng_load_session_attr *attr);

/*
 * Return the configuration override control URL attribute. This attribute
 * indicates a control URL override to be applied during the loading of the
 * configuration(s).
 *
 * NULL indicates no control URL override will be applied on configuration load.
 */
LTTNG_EXPORT extern const char *
lttng_load_session_attr_get_override_ctrl_url(struct lttng_load_session_attr *attr);

/*
 * Return the configuration override data URL attribute. This attribute
 * indicates a data URL override to be applied during the loading of the
 * configuration(s).
 *
 * NULL indicates no data URL override will be applied on configuration load.
 */
LTTNG_EXPORT extern const char *
lttng_load_session_attr_get_override_data_url(struct lttng_load_session_attr *attr);

/*
 * Return the configuration override session name attribute.
 * This attribute indicates a session name override to be applied during
 * the loading of the configuration(s).
 *
 * NULL indicates no session name override will be applied on configuration
 * load.
 */
LTTNG_EXPORT extern const char *
lttng_load_session_attr_get_override_session_name(struct lttng_load_session_attr *attr);

/*
 * Load session attribute setter family of functions.
 *
 * For every set* call, 0 is returned on success or else -LTTNG_ERR_INVALID is
 * returned indicating that at least one given parameter is invalid.
 */

/*
 * Set the name of the session to load. A NULL name means all sessions
 * found at the input URL will be loaded.
 */
LTTNG_EXPORT extern int
lttng_load_session_attr_set_session_name(struct lttng_load_session_attr *attr,
					 const char *session_name);

/*
 * Set the URL of the session configuration to load. A NULL value indicates the
 * use of the default session configuration location.
 *
 * Note that file:// is the only supported URL format.
 */
LTTNG_EXPORT extern int lttng_load_session_attr_set_input_url(struct lttng_load_session_attr *attr,
							      const char *url);

/*
 * Set the overwrite attribute. If set to true, current sessions matching the
 * loaded sessions will be destroyed and be replaced by the session(s) being
 * loaded.
 */
LTTNG_EXPORT extern int lttng_load_session_attr_set_overwrite(struct lttng_load_session_attr *attr,
							      int overwrite);

/*
 * The following setter are for overriding sessions attributes during the
 * loading of a configuration files. Those attributes prevail upon those
 * specified in the loaded configuration file.
 * */

/*
 * Set the url override attribute.
 *
 * Supported format:
 *    file://TRACEPATH
 *    NETPROTO://(HOST | IPADDR)[:CTRLPORT[:DATAPORT]][/TRACEPATH]
 *
 *     Where NETPROTO is one of {tcp, tcp6}
 *
 * See lttng-create(1) for more detail.
 */
LTTNG_EXPORT extern int
lttng_load_session_attr_set_override_url(struct lttng_load_session_attr *attr, const char *url);

/*
 * Set the control url override attribute.
 *
 * Supported format:
 *     NETPROTO://(HOST | IPADDR)[:PORT][/TRACEPATH]
 *
 *     Where NETPROTO is one of {tcp, tcp6}
 *
 * See lttng-create(1) for more detail.
 */
LTTNG_EXPORT extern int
lttng_load_session_attr_set_override_ctrl_url(struct lttng_load_session_attr *attr,
					      const char *url);

/*
 * Set the data url override attribute.
 *
 * Supported format:
 *     NETPROTO://(HOST | IPADDR)[:PORT][/TRACEPATH]
 *
 *     Where NETPROTO is one of {tcp, tcp6}
 *
 * See lttng-create(1) for more detail.
 */
LTTNG_EXPORT extern int
lttng_load_session_attr_set_override_data_url(struct lttng_load_session_attr *attr,
					      const char *url);

/*
 * Set the session name override attribute.
 *
 * Loading a configuration file defining multiple sessions will fail if a
 * session name is provided.
 */
LTTNG_EXPORT extern int
lttng_load_session_attr_set_override_session_name(struct lttng_load_session_attr *attr,
						  const char *session_name);

/*
 * Load session configuration(s).
 *
 * The lttng_load_session_attr object must not be NULL. No ownership of the
 * object is kept by the function; it must be released by the caller.
 *
 * Returns 0 on success or a negative LTTNG_ERR value on error.
 */
LTTNG_EXPORT extern int lttng_load_session(struct lttng_load_session_attr *attr);

#ifdef __cplusplus
}
#endif

#endif /* LTTNG_LOAD_H */
