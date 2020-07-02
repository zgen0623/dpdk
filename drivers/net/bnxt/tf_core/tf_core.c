/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2019-2020 Broadcom
 * All rights reserved.
 */

#include <stdio.h>

#include "tf_core.h"
#include "tf_util.h"
#include "tf_session.h"
#include "tf_tbl.h"
#include "tf_em.h"
#include "tf_rm.h"
#include "tf_msg.h"
#include "tfp.h"
#include "bitalloc.h"
#include "bnxt.h"
#include "rand.h"
#include "tf_common.h"
#include "hwrm_tf.h"

int
tf_open_session(struct tf *tfp,
		struct tf_open_session_parms *parms)
{
	int rc;
	unsigned int domain, bus, slot, device;
	struct tf_session_open_session_parms oparms;

	TF_CHECK_PARMS2(tfp, parms);

	/* Filter out any non-supported device types on the Core
	 * side. It is assumed that the Firmware will be supported if
	 * firmware open session succeeds.
	 */
	if (parms->device_type != TF_DEVICE_TYPE_WH) {
		TFP_DRV_LOG(ERR,
			    "Unsupported device type %d\n",
			    parms->device_type);
		return -ENOTSUP;
	}

	/* Verify control channel and build the beginning of session_id */
	rc = sscanf(parms->ctrl_chan_name,
		    "%x:%x:%x.%d",
		    &domain,
		    &bus,
		    &slot,
		    &device);
	if (rc != 4) {
		TFP_DRV_LOG(ERR,
			    "Failed to scan device ctrl_chan_name\n");
		return -EINVAL;
	}

	parms->session_id.internal.domain = domain;
	parms->session_id.internal.bus = bus;
	parms->session_id.internal.device = device;
	oparms.open_cfg = parms;

	rc = tf_session_open_session(tfp, &oparms);
	/* Logging handled by tf_session_open_session */
	if (rc)
		return rc;

	TFP_DRV_LOG(INFO,
		    "Session created, session_id:%d\n",
		    parms->session_id.id);

	TFP_DRV_LOG(INFO,
		    "domain:%d, bus:%d, device:%d, fw_session_id:%d\n",
		    parms->session_id.internal.domain,
		    parms->session_id.internal.bus,
		    parms->session_id.internal.device,
		    parms->session_id.internal.fw_session_id);

	return 0;
}

int
tf_attach_session(struct tf *tfp,
		  struct tf_attach_session_parms *parms)
{
	int rc;
	unsigned int domain, bus, slot, device;
	struct tf_session_attach_session_parms aparms;

	TF_CHECK_PARMS2(tfp, parms);

	/* Verify control channel */
	rc = sscanf(parms->ctrl_chan_name,
		    "%x:%x:%x.%d",
		    &domain,
		    &bus,
		    &slot,
		    &device);
	if (rc != 4) {
		TFP_DRV_LOG(ERR,
			    "Failed to scan device ctrl_chan_name\n");
		return -EINVAL;
	}

	/* Verify 'attach' channel */
	rc = sscanf(parms->attach_chan_name,
		    "%x:%x:%x.%d",
		    &domain,
		    &bus,
		    &slot,
		    &device);
	if (rc != 4) {
		TFP_DRV_LOG(ERR,
			    "Failed to scan device attach_chan_name\n");
		return -EINVAL;
	}

	/* Prepare return value of session_id, using ctrl_chan_name
	 * device values as it becomes the session id.
	 */
	parms->session_id.internal.domain = domain;
	parms->session_id.internal.bus = bus;
	parms->session_id.internal.device = device;
	aparms.attach_cfg = parms;
	rc = tf_session_attach_session(tfp,
				       &aparms);
	/* Logging handled by dev_bind */
	if (rc)
		return rc;

	TFP_DRV_LOG(INFO,
		    "Attached to session, session_id:%d\n",
		    parms->session_id.id);

	TFP_DRV_LOG(INFO,
		    "domain:%d, bus:%d, device:%d, fw_session_id:%d\n",
		    parms->session_id.internal.domain,
		    parms->session_id.internal.bus,
		    parms->session_id.internal.device,
		    parms->session_id.internal.fw_session_id);

	return rc;
}

int
tf_close_session(struct tf *tfp)
{
	int rc;
	struct tf_session_close_session_parms cparms = { 0 };
	union tf_session_id session_id = { 0 };
	uint8_t ref_count;

	TF_CHECK_PARMS1(tfp);

	cparms.ref_count = &ref_count;
	cparms.session_id = &session_id;
	rc = tf_session_close_session(tfp,
				      &cparms);
	/* Logging handled by tf_session_close_session */
	if (rc)
		return rc;

	TFP_DRV_LOG(INFO,
		    "Closed session, session_id:%d, ref_count:%d\n",
		    cparms.session_id->id,
		    *cparms.ref_count);

	TFP_DRV_LOG(INFO,
		    "domain:%d, bus:%d, device:%d, fw_session_id:%d\n",
		    cparms.session_id->internal.domain,
		    cparms.session_id->internal.bus,
		    cparms.session_id->internal.device,
		    cparms.session_id->internal.fw_session_id);

	return rc;
}

/** insert EM hash entry API
 *
 *    returns:
 *    0       - Success
 *    -EINVAL - Error
 */
int tf_insert_em_entry(struct tf *tfp,
		       struct tf_insert_em_entry_parms *parms)
{
	struct tf_session      *tfs;
	struct tf_dev_info     *dev;
	int rc;

	TF_CHECK_PARMS2(tfp, parms);

	/* Retrieve the session information */
	rc = tf_session_get_session(tfp, &tfs);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup session, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	/* Retrieve the device information */
	rc = tf_session_get_device(tfs, &dev);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup device, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	if (parms->mem == TF_MEM_EXTERNAL &&
		dev->ops->tf_dev_insert_ext_em_entry != NULL)
		rc = dev->ops->tf_dev_insert_ext_em_entry(tfp, parms);
	else if (parms->mem == TF_MEM_INTERNAL &&
		dev->ops->tf_dev_insert_int_em_entry != NULL)
		rc = dev->ops->tf_dev_insert_int_em_entry(tfp, parms);
	else
		return -EINVAL;

	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: EM insert failed, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	return 0;
}

/** Delete EM hash entry API
 *
 *    returns:
 *    0       - Success
 *    -EINVAL - Error
 */
int tf_delete_em_entry(struct tf *tfp,
		       struct tf_delete_em_entry_parms *parms)
{
	struct tf_session      *tfs;
	struct tf_dev_info     *dev;
	int rc;

	TF_CHECK_PARMS2(tfp, parms);

	/* Retrieve the session information */
	rc = tf_session_get_session(tfp, &tfs);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup session, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	/* Retrieve the device information */
	rc = tf_session_get_device(tfs, &dev);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup device, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	if (parms->mem == TF_MEM_EXTERNAL)
		rc = dev->ops->tf_dev_delete_ext_em_entry(tfp, parms);
	else if (parms->mem == TF_MEM_INTERNAL)
		rc = dev->ops->tf_dev_delete_int_em_entry(tfp, parms);
	else
		return -EINVAL;

	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: EM delete failed, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	return rc;
}

int
tf_alloc_identifier(struct tf *tfp,
		    struct tf_alloc_identifier_parms *parms)
{
	int rc;
	struct tf_session *tfs;
	struct tf_dev_info *dev;
	struct tf_ident_alloc_parms aparms;
	uint16_t id;

	TF_CHECK_PARMS2(tfp, parms);

	/* Can't do static initialization due to UT enum check */
	memset(&aparms, 0, sizeof(struct tf_ident_alloc_parms));

	/* Retrieve the session information */
	rc = tf_session_get_session(tfp, &tfs);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup session, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	/* Retrieve the device information */
	rc = tf_session_get_device(tfs, &dev);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup device, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	if (dev->ops->tf_dev_alloc_ident == NULL) {
		rc = -EOPNOTSUPP;
		TFP_DRV_LOG(ERR,
			    "%s: Operation not supported, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return -EOPNOTSUPP;
	}

	aparms.dir = parms->dir;
	aparms.type = parms->ident_type;
	aparms.id = &id;
	rc = dev->ops->tf_dev_alloc_ident(tfp, &aparms);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Identifier allocation failed, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	parms->id = id;

	return 0;
}

int
tf_free_identifier(struct tf *tfp,
		   struct tf_free_identifier_parms *parms)
{
	int rc;
	struct tf_session *tfs;
	struct tf_dev_info *dev;
	struct tf_ident_free_parms fparms;

	TF_CHECK_PARMS2(tfp, parms);

	/* Can't do static initialization due to UT enum check */
	memset(&fparms, 0, sizeof(struct tf_ident_free_parms));

	/* Retrieve the session information */
	rc = tf_session_get_session(tfp, &tfs);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup session, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	/* Retrieve the device information */
	rc = tf_session_get_device(tfs, &dev);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup device, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	if (dev->ops->tf_dev_free_ident == NULL) {
		rc = -EOPNOTSUPP;
		TFP_DRV_LOG(ERR,
			    "%s: Operation not supported, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return -EOPNOTSUPP;
	}

	fparms.dir = parms->dir;
	fparms.type = parms->ident_type;
	fparms.id = parms->id;
	rc = dev->ops->tf_dev_free_ident(tfp, &fparms);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Identifier free failed, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	return 0;
}

int
tf_alloc_tcam_entry(struct tf *tfp,
		    struct tf_alloc_tcam_entry_parms *parms)
{
	int rc;
	struct tf_session *tfs;
	struct tf_dev_info *dev;
	struct tf_tcam_alloc_parms aparms = { 0 };

	TF_CHECK_PARMS2(tfp, parms);

	/* Retrieve the session information */
	rc = tf_session_get_session(tfp, &tfs);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup session, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	/* Retrieve the device information */
	rc = tf_session_get_device(tfs, &dev);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup device, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	if (dev->ops->tf_dev_alloc_tcam == NULL) {
		rc = -EOPNOTSUPP;
		TFP_DRV_LOG(ERR,
			    "%s: Operation not supported, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	aparms.dir = parms->dir;
	aparms.type = parms->tcam_tbl_type;
	aparms.key_size = TF_BITS2BYTES_WORD_ALIGN(parms->key_sz_in_bits);
	aparms.priority = parms->priority;
	rc = dev->ops->tf_dev_alloc_tcam(tfp, &aparms);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: TCAM allocation failed, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	parms->idx = aparms.idx;

	return 0;
}

int
tf_set_tcam_entry(struct tf *tfp,
		  struct tf_set_tcam_entry_parms *parms)
{
	int rc;
	struct tf_session *tfs;
	struct tf_dev_info *dev;
	struct tf_tcam_set_parms sparms = { 0 };

	TF_CHECK_PARMS2(tfp, parms);

	/* Retrieve the session information */
	rc = tf_session_get_session(tfp, &tfs);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup session, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	/* Retrieve the device information */
	rc = tf_session_get_device(tfs, &dev);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup device, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	if (dev->ops->tf_dev_set_tcam == NULL) {
		rc = -EOPNOTSUPP;
		TFP_DRV_LOG(ERR,
			    "%s: Operation not supported, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	sparms.dir = parms->dir;
	sparms.type = parms->tcam_tbl_type;
	sparms.idx = parms->idx;
	sparms.key = parms->key;
	sparms.mask = parms->mask;
	sparms.key_size = TF_BITS2BYTES_WORD_ALIGN(parms->key_sz_in_bits);
	sparms.result = parms->result;
	sparms.result_size = TF_BITS2BYTES_WORD_ALIGN(parms->result_sz_in_bits);

	rc = dev->ops->tf_dev_set_tcam(tfp, &sparms);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: TCAM set failed, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	return 0;
}

int
tf_get_tcam_entry(struct tf *tfp __rte_unused,
		  struct tf_get_tcam_entry_parms *parms __rte_unused)
{
	TF_CHECK_PARMS2(tfp, parms);
	return -EOPNOTSUPP;
}

int
tf_free_tcam_entry(struct tf *tfp,
		   struct tf_free_tcam_entry_parms *parms)
{
	int rc;
	struct tf_session *tfs;
	struct tf_dev_info *dev;
	struct tf_tcam_free_parms fparms = { 0 };

	TF_CHECK_PARMS2(tfp, parms);

	/* Retrieve the session information */
	rc = tf_session_get_session(tfp, &tfs);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup session, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	/* Retrieve the device information */
	rc = tf_session_get_device(tfs, &dev);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup device, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	if (dev->ops->tf_dev_free_tcam == NULL) {
		rc = -EOPNOTSUPP;
		TFP_DRV_LOG(ERR,
			    "%s: Operation not supported, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	fparms.dir = parms->dir;
	fparms.type = parms->tcam_tbl_type;
	fparms.idx = parms->idx;
	rc = dev->ops->tf_dev_free_tcam(tfp, &fparms);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: TCAM free failed, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	return 0;
}

int
tf_alloc_tbl_entry(struct tf *tfp,
		   struct tf_alloc_tbl_entry_parms *parms)
{
	int rc;
	struct tf_session *tfs;
	struct tf_dev_info *dev;
	struct tf_tbl_alloc_parms aparms;
	uint32_t idx;

	TF_CHECK_PARMS2(tfp, parms);

	/* Can't do static initialization due to UT enum check */
	memset(&aparms, 0, sizeof(struct tf_tbl_alloc_parms));

	/* Retrieve the session information */
	rc = tf_session_get_session(tfp, &tfs);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup session, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	/* Retrieve the device information */
	rc = tf_session_get_device(tfs, &dev);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup device, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	if (dev->ops->tf_dev_alloc_tbl == NULL) {
		rc = -EOPNOTSUPP;
		TFP_DRV_LOG(ERR,
			    "%s: Operation not supported, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return -EOPNOTSUPP;
	}

	aparms.dir = parms->dir;
	aparms.type = parms->type;
	aparms.idx = &idx;
	rc = dev->ops->tf_dev_alloc_tbl(tfp, &aparms);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Table allocation failed, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	parms->idx = idx;

	return 0;
}

int
tf_free_tbl_entry(struct tf *tfp,
		  struct tf_free_tbl_entry_parms *parms)
{
	int rc;
	struct tf_session *tfs;
	struct tf_dev_info *dev;
	struct tf_tbl_free_parms fparms;

	TF_CHECK_PARMS2(tfp, parms);

	/* Can't do static initialization due to UT enum check */
	memset(&fparms, 0, sizeof(struct tf_tbl_free_parms));

	/* Retrieve the session information */
	rc = tf_session_get_session(tfp, &tfs);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup session, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	/* Retrieve the device information */
	rc = tf_session_get_device(tfs, &dev);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup device, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	if (dev->ops->tf_dev_free_tbl == NULL) {
		rc = -EOPNOTSUPP;
		TFP_DRV_LOG(ERR,
			    "%s: Operation not supported, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return -EOPNOTSUPP;
	}

	fparms.dir = parms->dir;
	fparms.type = parms->type;
	fparms.idx = parms->idx;
	rc = dev->ops->tf_dev_free_tbl(tfp, &fparms);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Table free failed, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	return 0;
}

int
tf_set_tbl_entry(struct tf *tfp,
		 struct tf_set_tbl_entry_parms *parms)
{
	int rc = 0;
	struct tf_session *tfs;
	struct tf_dev_info *dev;
	struct tf_tbl_set_parms sparms;

	TF_CHECK_PARMS3(tfp, parms, parms->data);

	/* Can't do static initialization due to UT enum check */
	memset(&sparms, 0, sizeof(struct tf_tbl_set_parms));

	/* Retrieve the session information */
	rc = tf_session_get_session(tfp, &tfs);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup session, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	/* Retrieve the device information */
	rc = tf_session_get_device(tfs, &dev);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup device, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	if (dev->ops->tf_dev_set_tbl == NULL) {
		rc = -EOPNOTSUPP;
		TFP_DRV_LOG(ERR,
			    "%s: Operation not supported, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return -EOPNOTSUPP;
	}

	sparms.dir = parms->dir;
	sparms.type = parms->type;
	sparms.data = parms->data;
	sparms.data_sz_in_bytes = parms->data_sz_in_bytes;
	sparms.idx = parms->idx;
	rc = dev->ops->tf_dev_set_tbl(tfp, &sparms);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Table set failed, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	return rc;
}

int
tf_get_tbl_entry(struct tf *tfp,
		 struct tf_get_tbl_entry_parms *parms)
{
	int rc = 0;
	struct tf_session *tfs;
	struct tf_dev_info *dev;
	struct tf_tbl_get_parms gparms;

	TF_CHECK_PARMS3(tfp, parms, parms->data);

	/* Can't do static initialization due to UT enum check */
	memset(&gparms, 0, sizeof(struct tf_tbl_get_parms));

	/* Retrieve the session information */
	rc = tf_session_get_session(tfp, &tfs);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup session, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	/* Retrieve the device information */
	rc = tf_session_get_device(tfs, &dev);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup device, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	if (dev->ops->tf_dev_get_tbl == NULL) {
		rc = -EOPNOTSUPP;
		TFP_DRV_LOG(ERR,
			    "%s: Operation not supported, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return -EOPNOTSUPP;
	}

	gparms.dir = parms->dir;
	gparms.type = parms->type;
	gparms.data = parms->data;
	gparms.data_sz_in_bytes = parms->data_sz_in_bytes;
	gparms.idx = parms->idx;
	rc = dev->ops->tf_dev_get_tbl(tfp, &gparms);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Table get failed, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	return rc;
}

int
tf_bulk_get_tbl_entry(struct tf *tfp,
		 struct tf_bulk_get_tbl_entry_parms *parms)
{
	int rc = 0;
	struct tf_session *tfs;
	struct tf_dev_info *dev;
	struct tf_tbl_get_bulk_parms bparms;

	TF_CHECK_PARMS2(tfp, parms);

	/* Can't do static initialization due to UT enum check */
	memset(&bparms, 0, sizeof(struct tf_tbl_get_bulk_parms));

	/* Retrieve the session information */
	rc = tf_session_get_session(tfp, &tfs);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup session, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	/* Retrieve the device information */
	rc = tf_session_get_device(tfs, &dev);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Failed to lookup device, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	if (parms->type == TF_TBL_TYPE_EXT) {
		/* Not supported, yet */
		rc = -EOPNOTSUPP;
		TFP_DRV_LOG(ERR,
			    "%s, External table type not supported, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));

		return rc;
	}

	/* Internal table type processing */

	if (dev->ops->tf_dev_get_bulk_tbl == NULL) {
		rc = -EOPNOTSUPP;
		TFP_DRV_LOG(ERR,
			    "%s: Operation not supported, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return -EOPNOTSUPP;
	}

	bparms.dir = parms->dir;
	bparms.type = parms->type;
	bparms.starting_idx = parms->starting_idx;
	bparms.num_entries = parms->num_entries;
	bparms.entry_sz_in_bytes = parms->entry_sz_in_bytes;
	bparms.physical_mem_addr = parms->physical_mem_addr;
	rc = dev->ops->tf_dev_get_bulk_tbl(tfp, &bparms);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "%s: Table get bulk failed, rc:%s\n",
			    tf_dir_2_str(parms->dir),
			    strerror(-rc));
		return rc;
	}

	return rc;
}

int
tf_alloc_tbl_scope(struct tf *tfp,
		   struct tf_alloc_tbl_scope_parms *parms)
{
	struct tf_session *tfs;
	struct tf_dev_info *dev;
	int rc;

	TF_CHECK_PARMS2(tfp, parms);

	/* Retrieve the session information */
	rc = tf_session_get_session(tfp, &tfs);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "Failed to lookup session, rc:%s\n",
			    strerror(-rc));
		return rc;
	}

	/* Retrieve the device information */
	rc = tf_session_get_device(tfs, &dev);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "Failed to lookup device, rc:%s\n",
			    strerror(-rc));
		return rc;
	}

	if (dev->ops->tf_dev_alloc_tbl_scope != NULL) {
		rc = dev->ops->tf_dev_alloc_tbl_scope(tfp, parms);
	} else {
		TFP_DRV_LOG(ERR,
			    "Alloc table scope not supported by device\n");
		return -EINVAL;
	}

	return rc;
}

int
tf_free_tbl_scope(struct tf *tfp,
		  struct tf_free_tbl_scope_parms *parms)
{
	struct tf_session *tfs;
	struct tf_dev_info *dev;
	int rc;

	TF_CHECK_PARMS2(tfp, parms);

	/* Retrieve the session information */
	rc = tf_session_get_session(tfp, &tfs);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "Failed to lookup session, rc:%s\n",
			    strerror(-rc));
		return rc;
	}

	/* Retrieve the device information */
	rc = tf_session_get_device(tfs, &dev);
	if (rc) {
		TFP_DRV_LOG(ERR,
			    "Failed to lookup device, rc:%s\n",
			    strerror(-rc));
		return rc;
	}

	if (dev->ops->tf_dev_free_tbl_scope) {
		rc = dev->ops->tf_dev_free_tbl_scope(tfp, parms);
	} else {
		TFP_DRV_LOG(ERR,
			    "Free table scope not supported by device\n");
		return -EINVAL;
	}

	return rc;
}
