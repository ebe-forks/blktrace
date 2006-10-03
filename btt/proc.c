/*
 * blktrace output analysis: generate a timeline & gather statistics
 *
 * Copyright (C) 2006 Alan D. Brunelle <Alan.Brunelle@hp.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <string.h>

#include "globals.h"

struct pn_info {
	struct rb_node rb_node;
	struct p_info *pip;
	union {
		char *name;
		__u32 pid;
	}  u;
};

struct rb_root root_pid, root_name;

struct p_info * __find_process_pid(__u32 pid)
{
	struct pn_info *this;
	struct rb_node *n = root_pid.rb_node;

	while (n) {
		this = rb_entry(n, struct pn_info, rb_node);
		if (pid < this->u.pid)
			n = n->rb_left;
		else if (pid > this->u.pid)
			n = n->rb_right;
		else
			return this->pip;
	}

	return NULL;
}

struct p_info *__find_process_name(char *name)
{
	int cmp;
	struct pn_info *this;
	struct rb_node *n = root_name.rb_node;

	while (n) {
		this = rb_entry(n, struct pn_info, rb_node);
		cmp = strcmp(name, this->u.name);
		if (cmp < 0)
			n = n->rb_left;
		else if (cmp > 0)
			n = n->rb_right;
		else
			return this->pip;
	}

	return NULL;
}

struct p_info *find_process(__u32 pid, char *name)
{
	struct p_info *pip;

	if (pid != ((__u32)-1) && ((pip = __find_process_pid(pid)) != NULL))
		return pip;

	if (name)
		return __find_process_name(name);

	return NULL;
}

static void insert_pid(struct p_info *that)
{
	struct pn_info *this;
	struct rb_node *parent = NULL;
	struct rb_node **p = &root_pid.rb_node;

	while (*p) {
		parent = *p;
		this = rb_entry(parent, struct pn_info, rb_node);

		if (that->pid < this->u.pid)
			p = &(*p)->rb_left;
		else if (that->pid > this->u.pid)
			p = &(*p)->rb_right;
		else {
			ASSERT(strcmp(that->name, this->pip->name) == 0);
			return;	// Already there
		}
	}

	this = malloc(sizeof(struct pn_info));
	this->u.pid = that->pid;
	this->pip = that;

	rb_link_node(&this->rb_node, parent, p);
	rb_insert_color(&this->rb_node, &root_pid);
}

static void insert_name(struct p_info *that)
{
	int cmp;
	struct pn_info *this;
	struct rb_node *parent = NULL;
	struct rb_node **p = &root_name.rb_node;

	while (*p) {
		parent = *p;
		this = rb_entry(parent, struct pn_info, rb_node);
		cmp = strcmp(that->name, this->u.name);

		if (cmp < 0)
			p = &(*p)->rb_left;
		else if (cmp > 0)
			p = &(*p)->rb_right;
		else
			return;	// Already there...
	}

	this = malloc(sizeof(struct pn_info));
	this->u.name = strdup(that->name);
	this->pip = that;

	rb_link_node(&this->rb_node, parent, p);
	rb_insert_color(&this->rb_node, &root_name);
}

static void insert(struct p_info *pip)
{
	insert_pid(pip);
	insert_name(pip);
}

void add_process(__u32 pid, char *name)
{
	struct p_info *pip = find_process(pid, name);

	if (pip == NULL) {
		size_t len = sizeof(struct p_info) + strlen(name) + 1;

		pip = memset(malloc(len), 0, len);
		pip->pid = pid;
		init_region(&pip->regions);
		pip->last_q = (__u64)-1;
		strcpy(pip->name, name);

		insert(pip);
	}
}

void pip_update_q(struct io *iop)
{
	if (iop->pip) {
		update_lq(&iop->pip->last_q, &iop->pip->avgs.q2q, iop->t.time);
		update_qregion(&iop->pip->regions, iop->t.time);
	}
}

void __foreach(struct rb_node *n, void (*f)(struct p_info *, void *), void *arg)
{
	if (n) {
		__foreach(n->rb_left, f, arg);
		f(rb_entry(n, struct pn_info, rb_node)->pip, arg);
		__foreach(n->rb_right, f, arg);
	}
}

void pip_foreach_out(void (*f)(struct p_info *, void *), void *arg)
{
	if (exes == NULL)
		__foreach(root_name.rb_node, f, arg);
	else {
		struct p_info *pip;
		char *exe, *p, *next, *exes_save = strdup(exes);

		p = exes_save;
		while (exes_save != NULL) {
			exe = exes_save;
			if ((next = strchr(exes_save, ',')) != NULL) {
				*next = '\0';
				exes_save = next+1;
			}
			else
				exes_save = NULL;

			pip = __find_process_name(exe);
			if (pip)
				f(pip, arg);
		}
	}
}
