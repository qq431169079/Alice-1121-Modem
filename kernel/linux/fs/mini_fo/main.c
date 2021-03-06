/*
 * Copyright (c) 1997-2003 Erez Zadok
 * Copyright (c) 2001-2003 Stony Brook University
 *
 * For specific licensing information, see the COPYING file distributed with
 * this package, or get one from ftp://ftp.filesystems.org/pub/fist/COPYING.
 *
 * This Copyright notice must be kept intact and distributed with all
 * fistgen sources INCLUDING sources generated by fistgen.
 */
/*
 * Copyright (C) 2004, 2005 Markus Klotzbuecher <mk@creamnet.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 *  $Id$
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "fist.h"
#include "mini_fo.h"
#include <linux/module.h>

/* This definition must only appear after we include <linux/module.h> */
#ifndef MODULE_LICENSE
# define MODULE_LICENSE(bison)
#endif /* not MODULE_LICENSE */

/*
 * This is the mini_fo tri interpose function, which extends the
 * functionality of the regular interpose by interposing a higher
 * level inode on top of two lower level ones: the base filesystem
 * inode and the storage filesystem inode.
 *
 *  sb we pass is mini_fo's super_block
 */
int
mini_fo_tri_interpose(dentry_t *hidden_dentry,
		      dentry_t *hidden_sto_dentry,
		      dentry_t *dentry, super_block_t *sb, int flag)
{
	inode_t *hidden_inode = NULL;
	inode_t *hidden_sto_inode = NULL; /* store corresponding storage inode */
	int err = 0;
	inode_t *inode;

	/* Pointer to hidden_sto_inode if exists, else to hidden_inode.
	 * This is used to copy the attributes of the correct inode. */
	inode_t *master_inode;

	if(hidden_dentry)
		hidden_inode = hidden_dentry->d_inode;
	if(hidden_sto_dentry)
		hidden_sto_inode = hidden_sto_dentry->d_inode;

	ASSERT(dentry->d_inode == NULL);

	/* mk: One of the inodes associated with the dentrys is likely to
	 * be NULL, so carefull:
	 */
	ASSERT((hidden_inode != NULL) || (hidden_sto_inode != NULL));

	if(hidden_sto_inode)
		master_inode = hidden_sto_inode;
	else
		master_inode = hidden_inode;

	/*
	 * We allocate our new inode below, by calling iget.
	 * iget will call our read_inode which will initialize some
	 * of the new inode's fields
	 */

	/*
	 * original: inode = iget(sb, hidden_inode->i_ino);
	 */
	inode = iget(sb, iunique(sb, 25));
	if (!inode) {
		err = -EACCES;		/* should be impossible??? */
		goto out;
	}

	/*
	 * interpose the inode if not already interposed
	 *   this is possible if the inode is being reused
	 * XXX: what happens if we get_empty_inode() but there's another already?
	 * for now, ASSERT() that this can't happen; fix later.
	 */
	if (itohi(inode) != NULL) {
		printk(KERN_CRIT "mini_fo_tri_interpose: itohi(inode) != NULL.\n");
	}
	if (itohi2(inode) != NULL) {
		printk(KERN_CRIT "mini_fo_tri_interpose: itohi2(inode) != NULL.\n");
	}

	/* mk: Carefull, igrab can't handle NULL inodes (ok, why should it?), so
	 * we need to check here:
	 */
	if(hidden_inode)
		itohi(inode) = igrab(hidden_inode);
	else
		itohi(inode) = NULL;

	if(hidden_sto_inode)
		itohi2(inode) = igrab(hidden_sto_inode);
	else
		itohi2(inode) = NULL;


	/* Use different set of inode ops for symlinks & directories*/
	if (S_ISLNK(master_inode->i_mode))
		inode->i_op = &mini_fo_symlink_iops;
	else if (S_ISDIR(master_inode->i_mode))
		inode->i_op = &mini_fo_dir_iops;

	/* Use different set of file ops for directories */
	if (S_ISDIR(master_inode->i_mode))
		inode->i_fop = &mini_fo_dir_fops;

	/* properly initialize special inodes */
	if (S_ISBLK(master_inode->i_mode) || S_ISCHR(master_inode->i_mode) ||
	    S_ISFIFO(master_inode->i_mode) || S_ISSOCK(master_inode->i_mode)) {
		init_special_inode(inode, master_inode->i_mode, master_inode->i_rdev);
	}

	/* Fix our inode's address operations to that of the lower inode */
	if (inode->i_mapping->a_ops != master_inode->i_mapping->a_ops) {
		inode->i_mapping->a_ops = master_inode->i_mapping->a_ops;
	}

	/* only (our) lookup wants to do a d_add */
	if (flag)
		d_add(dentry, inode);
	else
		d_instantiate(dentry, inode);

	ASSERT(dtopd(dentry) != NULL);

	/* all well, copy inode attributes */
	fist_copy_attr_all(inode, master_inode);

 out:
	return err;
}

/* parse mount options "base=" and "sto=" */
dentry_t *
mini_fo_parse_options(super_block_t *sb, char *options)
{
	dentry_t *hidden_root = ERR_PTR(-EINVAL);
	dentry_t *hidden_root2 = ERR_PTR(-EINVAL);
	struct nameidata nd, nd2;
	char *name, *tmp, *end;
	int err = 0;

	/* We don't want to go off the end of our arguments later on. */
	for (end = options; *end; end++);

	while (options < end) {
		tmp = options;
		while (*tmp && *tmp != ',')
			tmp++;
		*tmp = '\0';
		if (!strncmp("base=", options, 5)) {
			name = options + 5;
			printk(KERN_INFO "mini_fo: using base directory: %s\n", name);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
			if (path_init(name, LOOKUP_FOLLOW, &nd))
				err = path_walk(name, &nd);
#else
			err = path_lookup(name, LOOKUP_FOLLOW, &nd);
#endif
			if (err) {
				printk(KERN_CRIT "mini_fo: error accessing hidden directory '%s'\n", name);
				hidden_root = ERR_PTR(err);
				goto out;
			}
			hidden_root = nd.dentry;
			stopd(sb)->base_dir_dentry = nd.dentry;
			stopd(sb)->hidden_mnt = nd.mnt;

		} else if(!strncmp("sto=", options, 4)) {
			/* parse the storage dir */
			name = options + 4;
			printk(KERN_INFO "mini_fo: using storage directory: %s\n", name);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
			if(path_init(name, LOOKUP_FOLLOW, &nd2))
				err = path_walk(name, &nd2);
#else
			err = path_lookup(name, LOOKUP_FOLLOW, &nd2);
#endif
			if(err) {
				printk(KERN_CRIT "mini_fo: error accessing hidden storage directory '%s'\n", name);

				hidden_root2 = ERR_PTR(err);
				goto out;
			}
			hidden_root2 = nd2.dentry;
			stopd(sb)->storage_dir_dentry = nd2.dentry;
			stopd(sb)->hidden_mnt2 = nd2.mnt;
			stohs2(sb) = hidden_root2->d_sb;

			/* validate storage dir, this is done in
			 * mini_fo_read_super for the base directory.
			 */
			if (IS_ERR(hidden_root2)) {
				printk(KERN_WARNING "mini_fo_parse_options: storage dentry lookup failed (err = %ld)\n", PTR_ERR(hidden_root2));
				goto out;
			}
			if (!hidden_root2->d_inode) {
				printk(KERN_WARNING "mini_fo_parse_options: no storage dir to interpose on.\n");
				goto out;
			}
			stohs2(sb) = hidden_root2->d_sb;
		} else {
			printk(KERN_WARNING "mini_fo: unrecognized option '%s'\n", options);
			hidden_root = ERR_PTR(-EINVAL);
			goto out;
		}
		options = tmp + 1;
	}

 out:
	if(IS_ERR(hidden_root2))
		return hidden_root2;
	return hidden_root;
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static int
#else
super_block_t *
#endif
mini_fo_read_super(super_block_t *sb, void *raw_data, int silent)
{
	dentry_t *hidden_root;
	int err = 0;

	if (!raw_data) {
		printk(KERN_WARNING "mini_fo_read_super: missing argument\n");
		err = -EINVAL;
		goto out;
	}
	/*
	 * Allocate superblock private data
	 */
	__stopd(sb) = kmalloc(sizeof(struct mini_fo_sb_info), GFP_KERNEL);
	if (!stopd(sb)) {
		printk(KERN_WARNING "%s: out of memory\n", __FUNCTION__);
		err = -ENOMEM;
		goto out;
	}
	stohs(sb) = NULL;

	hidden_root = mini_fo_parse_options(sb, raw_data);
	if (IS_ERR(hidden_root)) {
		printk(KERN_WARNING "mini_fo_read_super: lookup_dentry failed (err = %ld)\n", PTR_ERR(hidden_root));
		err = PTR_ERR(hidden_root);
		goto out_free;
	}
	if (!hidden_root->d_inode) {
		printk(KERN_WARNING "mini_fo_read_super: no directory to interpose on\n");
		goto out_free;
	}
	stohs(sb) = hidden_root->d_sb;

	/*
	 * Linux 2.4.2-ac3 and beyond has code in
	 * mm/filemap.c:generic_file_write() that requires sb->s_maxbytes
	 * to be populated.  If not set, all write()s under that sb will
	 * return 0.
	 *
	 * Linux 2.4.4+ automatically sets s_maxbytes to MAX_NON_LFS;
	 * the filesystem should override it only if it supports LFS.
	 */
	/* non-SCA code is good to go with LFS */
	sb->s_maxbytes = hidden_root->d_sb->s_maxbytes;

	sb->s_op = &mini_fo_sops;
	/*
	 * we can't use d_alloc_root if we want to use
	 * our own interpose function unchanged,
	 * so we simply replicate *most* of the code in d_alloc_root here
	 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	sb->s_root = d_alloc(NULL, &(const struct qstr) { "/", 1, 0 });
#else
	sb->s_root = d_alloc(NULL, &(const struct qstr){hash: 0, name: "/", len : 1});
#endif
	if (IS_ERR(sb->s_root)) {
		printk(KERN_WARNING "mini_fo_read_super: d_alloc failed\n");
		err = -ENOMEM;
		goto out_dput;
	}

	sb->s_root->d_op = &mini_fo_dops;
	sb->s_root->d_sb = sb;
	sb->s_root->d_parent = sb->s_root;

	/* link the upper and lower dentries */
	__dtopd(sb->s_root) = (struct mini_fo_dentry_info *)
		kmalloc(sizeof(struct mini_fo_dentry_info), GFP_KERNEL);
	if (!dtopd(sb->s_root)) {
		err = -ENOMEM;
		goto out_dput2;
	}
	dtopd(sb->s_root)->state = MODIFIED;
	dtohd(sb->s_root) = hidden_root;

	/* fanout relevant, interpose on storage root dentry too */
	dtohd2(sb->s_root) = stopd(sb)->storage_dir_dentry;

	/* ...and call tri-interpose to interpose root dir inodes
	 * if (mini_fo_interpose(hidden_root, sb->s_root, sb, 0))
	 */
	if(mini_fo_tri_interpose(hidden_root, dtohd2(sb->s_root), sb->s_root, sb, 0))
		goto out_dput2;

	/* initalize the wol list */
	itopd(sb->s_root->d_inode)->deleted_list_size = -1;
	itopd(sb->s_root->d_inode)->renamed_list_size = -1;
	meta_build_lists(sb->s_root);

	goto out;

 out_dput2:
	dput(sb->s_root);
 out_dput:
	dput(hidden_root);
	dput(dtohd2(sb->s_root)); /* release the hidden_sto_dentry too */
 out_free:
	kfree(stopd(sb));
	__stopd(sb) = NULL;
 out:

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	return err;
#else
	if (err) {
		return ERR_PTR(err);
	} else {
		return sb;
	}
#endif
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static struct super_block *mini_fo_get_sb(struct file_system_type *fs_type,
					  int flags, const char *dev_name,
					  void *raw_data)
{
	return get_sb_nodev(fs_type, flags, raw_data, mini_fo_read_super);
}

void mini_fo_kill_block_super(struct super_block *sb)
{
	generic_shutdown_super(sb);
	/*
	 *      XXX: BUG: Halcrow: Things get unstable sometime after this point:
	 *      lib/rwsem-spinlock.c:127: spin_is_locked on uninitialized
	 *      fs/fs-writeback.c:402: spin_lock(fs/super.c:a0381828) already
	 *      locked by fs/fs-writeback.c/402
	 *
	 *      Apparently, someone's not releasing a lock on sb_lock...
	 */
}

static struct file_system_type mini_fo_fs_type = {
	.owner          = THIS_MODULE,
	.name           = "mini_fo",
	.get_sb         = mini_fo_get_sb,
	.kill_sb        = mini_fo_kill_block_super,
	.fs_flags       = 0,
};


#else
static DECLARE_FSTYPE(mini_fo_fs_type, "mini_fo", mini_fo_read_super, 0);
#endif

static int __init init_mini_fo_fs(void)
{
	printk("Registering mini_fo version $Id$\n");
	return register_filesystem(&mini_fo_fs_type);
}
static void __exit exit_mini_fo_fs(void)
{
	printk("Unregistering mini_fo version $Id$\n");
	unregister_filesystem(&mini_fo_fs_type);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
EXPORT_NO_SYMBOLS;
#endif

MODULE_AUTHOR("Erez Zadok <ezk@cs.sunysb.edu>");
MODULE_DESCRIPTION("FiST-generated mini_fo filesystem");
MODULE_LICENSE("GPL");

/* MODULE_PARM(fist_debug_var, "i"); */
/* MODULE_PARM_DESC(fist_debug_var, "Debug level"); */

module_init(init_mini_fo_fs)
module_exit(exit_mini_fo_fs)
