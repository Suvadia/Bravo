//============================================================================
// Name        : script.cpp
// Author      : Howard Brown 
// Version     : 2.00
// Copyright   : 2003 - 2013 Suvadia Inc
// Description : Bravo Script Language class
//
//============================================================================
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h> 
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "ongthread.h"
#include "ongtimer.h"
#include "ongcontext.h"
#include "ongdatetime.h"
#include "ongstring.h"
#include "bravodatabase.h"
#include "bravomemory.h"
#include "bravobuffer.h"
#include "bravobook.h"
#include "bravocodec.h"
#include "bravosession.h"
#include "bravostreams.h"
#include "bravoevents.h"
#include "bravocodec.h"
#include "bravotune.h"
#include "bravomixer.h"
#include "bravoscript.h"
#include "bravoplaylist.h"
#include "MersenneTwister.h"
#include "bravoscriptutils.h"

#define ARGUMENTS_IML MAIN_IMLS[0]
extern BRAVOSCRIPTIML VAR_IML;
extern BRAVOSCRIPTIML DEFAULT_IML;
extern BRAVOSCRIPTIML RW_IMLS[];
extern BRAVOSCRIPTIML MAIN_IMLS[];
extern BRAVOSCRIPTIML BRAVO_IMLS[];
extern BRAVOSCRIPTIML LIB_IMLS[];
extern BRAVOSCRIPTIML EVENTS_IMLS[];
extern BRAVOSCRIPTIML TUNES_IMLS[];
extern BRAVOSCRIPTIML ALBUMS_IMLS[];
extern BRAVOSCRIPTIML RIDDIMS_IMLS[];
extern BRAVOSCRIPTIML ARTISTS_IMLS[];
extern BRAVOSCRIPTIML PROPERTY_IMLS[];
extern BRAVOSCRIPTIML MATH_IMLS[];
extern BRAVOSCRIPTIML STRING_IMLS[];
extern BRAVOSCRIPTIML NUMBER_IMLS[];
extern BRAVOSCRIPTIML DATE_IMLS[];
extern BRAVOSCRIPTIML ARGUMENTS_IMLS[];
extern BRAVOSCRIPTIML PLAYLIST_IMLS[];
extern BRAVOSCRIPTIML STATION_IMLS[];
extern BRAVOSCRIPTIML CHANNEL_IMLS[];
extern BRAVOSCRIPTIML TUNE_IMLS[];

////////////////////////////////////////////////////////////////////////
//
// Bravo Script
//
////////////////////////////////////////////////////////////////////////
bravoScript::bravoScript(bravoContext *pContext) {
	m_pContext = pContext;
	m_Memory.init(ONGMEMORY_DEFAULTSIZE);
	m_pRand = new MTRand;
}
bravoScript::~bravoScript() {

}

////////////////////////////////////////////////////////////////////////
//
// bravoScript:createApp()
//
////////////////////////////////////////////////////////////////////////
bravoScriptApp *bravoScript::newApp(void *pPlaylist) {
	bravoScriptApp *app = new bravoScriptApp();

	app->m_pContext = this;
	if (pPlaylist ){

		if( !(app->loc = m_Memory.addNode())) {

			delete app;
			return NULL;
		}
		app->pl = pPlaylist;
		app->var = ((bravoPlaylist *) pPlaylist)->m_pNode;
	} else {

		app->var = app->loc = m_Memory.getRoot();
	}
	return app;
}

////////////////////////////////////////////////////////////////////////
//
// bravoScript:run()
//
////////////////////////////////////////////////////////////////////////
bool bravoScript::run(BRAVOSCRIPTCONTEXT *m) {
	int id, l;
	cpd com;
	BRAVOSCRIPTIML *pw;

	for (;;) {
		if (!src_skip(m)) {
			m->out = BRAVOSCRIPT_NONE;
			return true;
		}

		switch (*m->src) {
		case '{': {
			m->src++;
			com.push(BRAVOSCRIPT_NONE);
			continue;
		}
		case '}': {
			m->src++;
			if (!com.count()) {
				assert(0);
			}
			switch (com.pop()) {
			case BRAVOSCRIPT_NONE: {
				if (com.count() == 0 && m->level.count()) {
					m->out = BRAVOSCRIPT_NONE;
					return true;
				}
				break;
			}
			case BRAVOSCRIPT_EVENT: {
				m_EV.pData = m_MB.commit();
				if (!((bravoPlaylist *)
						m->app->pl)->m_Events.addEvent(&m_EV, m_FN)) {

					logerror(m, ONGERROR_MEMORYFAILURE, m->src);
					return false;
				}
				break;
			}
			case BRAVOSCRIPT_SWITCHERROR: {
				m_EH[0] = 0; // handle none
			}
			case BRAVOSCRIPT_CASEERROR: {
				m_EH[1] = -1; // fail condition undefined
				break;
			}
			}
			continue;
		}
		}
		// search reserved works
		id = (pw = iml_find(RW_IMLS, &m->src)) ? pw->id : BRAVOSCRIPT_NONE;
have_keyword:
		// skip junks
		if (!src_skip(m)) {
			assert(0);
			return false;
		}

		switch (id) {
		////////////////////////////////////////////////////////////////////
		// NONE
		////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_NONE:
		////////////////////////////////////////////////////////////////////
		// VAR
		////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_VAR: {
			m->sq.push(id);
			if (!eval(m))
				return false;
			m->sq.pop();

			if (*m->src++ != ';') {
				logerror(m, ONGERROR_SYNTAXERRORMISSING, m->src, ';');
				return false;
			}
			break;
		}
		////////////////////////////////////////////////////////////////////
		//  IF
		////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_IF: {
			m->par = *m->src; // starter
			if (*m->src++ != '(') {
				logerror(m, ONGERROR_SYNTAXERRORMISSING, m->src, '(');
				return false;
			}
			m->sq += id;
			if (!parse_if(m))
				return false;
			m->sq--;

			if (!m->out) { // skip condition block
				if (src_skip(m, 1)) {
					logerror(m, ONGERROR_SYNTAXERROR, m->src);
					return false;
				}
				// end of block
				m->src++;
				// skip junks
				if (!src_skip(m)) {
					m->out = BRAVOSCRIPT_NONE;
					return true;
				}
				if ((pw = iml_find(RW_IMLS, &m->src))) {
					id = pw->id;
					if (id != BRAVOSCRIPT_ELSE)
						goto have_keyword;
				}
			}
			break;
		}
		////////////////////////////////////////////////////////////////////
		// ELSE
		////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_ELSE: { // we just skip else block
			if ((l = src_skip(m, 1))) {
				logerror(m, ONGERROR_SYNTAXERRORMISSING, m->src, l);
				return false;
			}
			m->src++;
			break;
		}
		////////////////////////////////////////////////////////////////////
		// FOR | WHILE | DO
		////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_FOR:
		case BRAVOSCRIPT_WHILE:
		case BRAVOSCRIPT_DO: {
			if (!parse_loop(m, id))
				return false;

			if(m->out == BRAVOSCRIPT_RETURN)
				return true;

			break;
		}
		////////////////////////////////////////////////////////////////////
		//
		////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_BREAK:
		case BRAVOSCRIPT_CONTINUE: {
			switch (m->level.peek()) {
			case BRAVOSCRIPT_FOR:
			case BRAVOSCRIPT_WHILE:
			case BRAVOSCRIPT_DO: {
				m->out = id;
				return true;
			}
			}
			l = pw->nameLength;
			logerror(m, ONGERROR_ILLEGAL, (m->src - l), l);
			return false;
		}
		////////////////////////////////////////////////////////////////////
		// ERROR
		////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_SWITCHERROR:
		case BRAVOSCRIPT_CASEERROR: {
			if (*m->src != '(') {
				assert(0);
			}
			m->par = *m->src++;
			if (!parse_arg(m, 1, 1))
				return false;

			m_EH[id == BRAVOSCRIPT_SWITCHERROR ? 0 : 1] = *((carg *) m->list);

			if (*m->src == '{') {
				m->src++;
				com.push(id);
			}
			break;
		}
		////////////////////////////////////////////////////////////////////
		// RETURN [VALUE]
		////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_RETURN: {

			if (*m->src == ';') {

				m->rv.m_type = ONGNODE_UNDEFINED;
			} else {
				// return value
				m->par = 0;
				m->sq+= id;
				if (!eval(m))
					return false;

				if (*m->src != ';') {

					logerror(m, ONGERROR_SYNTAXERRORMISSING, m->src, ';');
					return false;
				}
			}
			m->src++;
			m->out = id;
			return true;
		}
		////////////////////////////////////////////////////////////////////
		// DELETE
		////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_DELETE: {
			l = src_scan(m->src);
			m->hvar = m->loc;
			for (;;) { // scan nodes
				if (!m_Memory.getNode(m->hvar, m->src, l, &m->hvar)) {
					if (m->hvar && m->hvar !=  m_Memory.getRoot() &&
							(m->hvar = m->hvar->parentNode())) {
						// up tree
						continue;
					}
					// undefined symbol
					logerror(m, ONGERROR_UNDEFINESYMBOL, m->src, l);
					return false;
				}
				break;
			}
			// delete it
			m_Memory.deleteNode(m->hvar);
			m->src += l;
			if ( !src_skip(m) || *m->src++ != ';') {
				logerror(m, ONGERROR_SYNTAXERRORMISSING, m->src, ';');
				return false;
			}
			break;
		}
		////////////////////////////////////////////////////////////////////
		// FUNCTION
		////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_FUNCTION: {
			if (!parse_function(m, false))
				return false;
			break;
		}
		////////////////////////////////////////////////////////////////////
		// IMPORT
		////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_IMPORT: {
			if (!parse_import(m))
				return false;
			break;
		}
		////////////////////////////////////////////////////////////////////
		// EVENT
		////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_EVENT: {
			if (com.find(id)) {
				logerror(m, ONGERROR_SYNTAXERROR, m->src);
				return false;
			}
			com.push(id);
			// clear event
			memset(&m_EV, 0, sizeof(ONGEVENT));
			m_MB.clear();
			m_EH[0] = 0; // handle none
			m_EH[1] = -1; // fail condition undefined

			// set channel
			m_EV.iChannel = m->ch;

			if (*m->src != '(') { // named event
				if ((l = src_scan(m->src))) {
					if (!m_MB.add(m->src, (l + 1), (void **) (void *) &m_EV.pName)) {
						logerror(m, ONGERROR_MEMORYFAILURE, m->src);
						return false;
					}
					m_EV.pName[l] = 0;
					// no duplicate allowed
					((bravoPlaylist *) m->app->pl)->m_Events.killEvent(m_EV.pName);
				}
				m->src += l;
				// skip junks and check for parameter
				if (!src_skip(m) || *m->src != '(') {
					logerror(m, ONGERROR_SYNTAXERRORMISSING, m->src, '(');
					return false;
				}
			}
			//do event parameters
			m->par = *m->src++; // step over '('
			if (!parse_arg(m, 4, 4))
				return false;

			for (l = 0; l < 4; l++) {
				carg *a = (m->argv + l);
				if (a->m_len == 0 || a->m_type != ONGNODE_NUMBER) {
					logerror(m, ONGERROR_INVALIDFUNCTIONPARAMETERS, m->src, l);
					free(m->pobj);
					return false;
				}
				switch (l) {
				case 0:	m_EV.iBaseOn = *a; break;
				case 1:	m_EV.iDue = msec2bytes(*a);break;
				case 2:	m_EV.iInterval = msec2bytes(*a);break;
				case 3:	m_EV.iRepeat = *a; break;
				}
			}
			free(m->pobj);
			if (*m->src++ != '{') {
				logerror(m, ONGERROR_SYNTAXERRORMISSING, m->src, '{');
				return false;
			}
			break;
		}
		////////////////////////////////////////////////////////////////////
		// FUNCTIONS
		////////////////////////////////////////////////////////////////////
		default: {
			// new event function
			if (!com.find(BRAVOSCRIPT_EVENT)) {
				logerror(m, ONGERROR_SYNTAXERROR, m->src);
				return false;
			}

			if (m_EV.iFunctions >= BRAVOSCRIPT_EFMAX) {
				logerror(m, ONGERROR_EVENTFUNCTIONS, m->src);
				return false;
			}

			ONGEVENTFUNCTION *pfn = &m_FN[m_EV.iFunctions++];
			int i;
			// clear struct
			memset(pfn, 0, sizeof(ONGEVENTFUNCTION));
			pfn->iFunction = (id - BRAVOSCRIPT_EVENT - 1);
			pfn->iErrorHandle = m_EH[0];
			pfn->iError = m_EH[1];
			// skip junks
			if (!src_skip(m) || *m->src != '(') {
				assert(0);
				return false;
			}
			m->par = *m->src++;
			if (!parse_arg(m, (int) pw->sub, (int) pw->sub))
				return false;

			if (m->out) {
				for (i = 0; i < m->out; i++) {
					carg *a = &((carg *) m->list)[i];
					void *d;
					pfn->iDataType[3 - i] = a->m_type;
					switch (a->m_type) {
					case ONGNODE_NUMBER:
					case ONGNODE_FUNCTION: {
						pfn->iParameter[i] = *a;
						continue;
					}
					case ONGNODE_STRING: {
						d = m_MB.add(a->m_data, a->m_len + 1,
								(void **)&pfn->pParameter[i]);
						break;
					}
					case ONGNODE_VAR: {
						// export var
						OngNode *node = (OngNode *) (PTR) *a;
						l = m_Memory.getNodeSize(node);
						if (!(d = m_MB.add(NULL, l, (void **)&pfn->pParameter[i]))
								|| node->pack(d, l) != l)
							d = NULL;
						break;
					}
					default: {
						printf("(event) not handle node type: %i", m->out);
						assert(0);
						return false;
					}
					}
					if (!d) {
						logerror(m, ONGERROR_MEMORYFAILURE, m->src);
						free(m->pobj);
						return false;
					}
				}
				free(m->pobj);
			}

			// skip junks
			if (!src_skip(m) || *m->src++ != ';') {
				logerror(m, ONGERROR_SYNTAXERRORMISSING, m->src, ';');
				return false;
			}
		}
		} // end switch (id)
	}// main loop
}
////////////////////////////////////////////////////////////////////////
//
// bravoScript::eval
//
////////////////////////////////////////////////////////////////////////
bool bravoScript::eval(BRAVOSCRIPTCONTEXT *m) {
	int op, l, mode, od, c;
	union {
	int      iv;
	void *   pv;
	};
	cpd step;
	BRAVOSCRIPTCONTEXT tm;

	if (!m->rv.alloc(BRAVONODE_DATASIZE)) {

		logerror(m, ONGERROR_MEMORYFAILURE, m->src);
		return false;
	}

	for (;;) {
		switch (step) {
		case 0: {
			m->rv.m_len = 0;
			m->rv.m_type = ONGNODE_NUMBER;
			m->out = ONGNODE_NUMBER;
			m->id = 0;
			m->hvar = NULL;
			mode = 0;
			op = OP_INI;
			od = OP_INI;

			step.pop();
			if (step)
				continue;

			step.push(1);
		}
		case 1: {

			if (!src_skip(m))
				return false;

			m->dis = ONGREADONLY;
			c = *m->src;
			if (isalpha(c) || c == '[') {
				m->list = 0;
				if (!parse(m))
					return false;

				if (m->dis == ONGVARIABLE) {
					// how we should pass off node to others
					l    = m->hvar->Length;
					mode = m->out == ONGNODE_STRING ? 1 : 0;
					switch (m->out) {
					case ONGNODE_NUMBER:

						iv = *m->hvar;
						break;
					case ONGNODE_STRING: {

						if (od == OP_AND) {
							pv 	   = m->hvar;
							m->out = ONGNODE_VAR;
							mode   = 0;
						} else {
							pv = m->hvar->m_pData;
						}
						break;
					}
					case ONGNODE_FUNCTION:

						pv = (void **) *m->hvar;
						break;
					case ONGNODE_ARRAY:
					case ONGNODE_OBJECT:
					case ONGNODE_VAR:
						// pass it's self
						pv     = m->hvar;
						m->out = ONGNODE_VAR;
						break;
						//	case ONGNODE_METHOD:
					case ONGNODE_DATE:
						///	case ONGNODE_TUNE:
						//	case ONGNODE_CHANNEL:
						//	case ONGNODE_PLAYLIST:
						pv = (void **) *m->hvar;
						break;
					case ONGNODE_UNDEFINED:
						iv = *m->hvar;
						break;
					default:
						printf("not handle node type: %i", m->out);
						assert(0);
						return false;
					}
				} else {
					// how we should pass off data to others
					mode = m->out == ONGNODE_STRING ? 1 : 0;
					switch (m->out) {
					case ONGNODE_NUMBER:
					case BRAVONODE_BOOLEAN:
					    pv = m->dis == ONGREADONLY ? m->pobj
								: ((void **) m->pobj)[0];
					   	break;
					case ONGNODE_STRING:

						pv = m->pobj;
						l  = strlen((char *) pv);
						break;
					case ONGNODE_DATE:
					case ONGNODE_TUNE:

						pv = m->pobj;
						break;
					case ONGNODE_UNDEFINED:

						iv = 0;
						break;
					default:
						printf("not handle type: %i %s", m->out, m->src);
						assert(0);
						return false;
					}
				}
			} else if (isdigit(c) || c == '.') {
				// constant number
				if( !src_number(m) ) {

					logerror(m, ONGERROR_SYNTAXERROR, m->src);
					return false;
				}
				mode = 0;
				iv   = m->iobj;
			} else {
				switch (c) {
				case '(': {
					tm = m;
					tm.par = *tm.src++;
					tm.sq.push(0);
					if (!eval(&tm))
						return false;
					if (tm.par != 0) {
						logerror(m, ONGERROR_SYNTAXERRORMISSING, tm.src, c);
						return false;
					}
					m->src = tm.src;
					m->ln = tm.ln;
					m->out = tm.rv.m_type;
					l = tm.rv.m_len;
					if (m->out == ONGNODE_STRING) {

						pv   =  tm.rv.m_data;
						mode = 1;
					} else {

						iv   = tm.rv;
						mode = 0;
					}
					break;
				}
				case '"': {
					// constant string
					const char *p = m->src;
					pv = (void *)p;

					if (src_len(&p, &l, &m->ln)) {

						logerror(m, ONGERROR_SYNTAXERROR, p);
						return false;
					}
					mode   = 2;
					m->out = ONGNODE_STRING;
					m->src = p;
					break;
				}
				case '\'': {
					// constant alpha number
					const char *p = m->src;
					int i;
					l  = 0;
					iv = 0;
					p++;
					while (*p++ != 0 && *p != '\'')
						l++;
					if (l > 4) {

						logerror(m, ONGERROR_SYNTAXERROR, p);
						return false;
					}
					char *dp = ((char *) &iv);
					for (i = 0; i <= l; i++)
						*dp++ = *--p;
					p += (i + 1);
					mode = 0;
					m->out = ONGNODE_NUMBER;
					m->src = p;
					break;
				}
				case ')':
				case ']': {
					if (m->par != c) {
						puts(m->src);
						logerror(m, ONGERROR_SYNTAXERROR, m->src);
						return false;
					}
					m->par--;
					if (op == OP_INI) {
						m->src++;
						return true;
					}
					break;
				}
				case '&':
					od = OP_AND;
					m->src++;
					continue;
				case '!':
					od = OP_NOT;
					m->src++;
					continue;
				case '~':
					od = OP_INV;
					m->src++;
					continue;
				case '-':
					iv     = 0;
					mode   = 0;
					m->out = ONGNODE_NUMBER;
					break;
				case ',':
				case '}':
					return true;
				default:
					printf("error exp(%c)~%s\r\n", c, m->src);
					assert(0);
					return false;
				}
			}
			step.pop();
			if (step)
				continue;
		}
		case 2:{ // step
			switch (od) {
			case OP_NOT:
				iv   = mode ? !l : !iv;
				mode = 0;
				break;
			case OP_INV:
				iv   = ~iv;
				mode = 0;
				break;
			case OP_INI:
				break;
			default: {
				logerror(m, ONGERROR_SYNTAXERROR, m->src);
				return false;
			}
			}

			if (op != OP_INI) {
				switch( m->out ){
				case ONGNODE_NUMBER:
				case ONGNODE_STRING:
				//case ONGNODE_DATE:
					switch( (int) m->rv.m_type ){
					case ONGNODE_STRING:
					case ONGNODE_NUMBER:
					//case ONGNODE_DATE:
						break;
					default:
						logerror(m, ONGERROR_SYNTAXERROR, m->src);
						return false;
					}
					break;
				default:
					logerror(m, ONGERROR_SYNTAXERROR, m->src);
					return false;
				}
			}

			switch (mode) {
			case 0: {
				// process number
				if (m->rv.m_type != ONGNODE_STRING) {
					m->rv.m_type = (ONGNODETYPE) m->out;
					m->rv.m_len = 4;
					register unsigned int *sum = (unsigned int *) m->rv.m_data;
					switch (op) {
					case OP_AND:
						*sum &= iv;
						break;
					case OP_SUB:
						*sum -= iv;
						break;
					case OP_ADD:
						*sum += iv;
						break;
					case OP_MUL:
						*sum *= iv;
						break;
					case OP_MOD:
						if (iv)
							*sum %= iv;
						break;
					case OP_DIV:
						if (iv)
							*sum /= iv;
						break;
					case OP_OR:
						*sum |= iv;
						break;
					case OP_XOR:
						*sum ^= iv;
						break;
					case OP_SHL:
						*sum <<= iv;
						break;
					case OP_SHR:
						*sum >>= iv;
						break;
					case OP_INI:
						*sum = iv;
						break;
					default: {
						logerror(m, ONGERROR_SYNTAXERROR, m->src);
						return false;
					}
					}
					break;
				}
			}
			case 1:
			case 2:{
				// process string
				if (op != OP_INI && op != OP_ADD) {
					logerror(m, ONGERROR_ILLEGAL, m->src, l);
					return false;
				}

				if (m->rv.m_len && m->rv.m_type == ONGNODE_NUMBER) {
					m->rv.m_len = 0;
					m->rv.writef("%d", *((int *) m->rv.m_data));
				}

				m->rv.m_type = ONGNODE_STRING;

				if (!mode) {

					m->rv.writef("%d", iv);
				} else {
					char * s;
					if (!(s = (char *) m->rv.write(NULL, (l + 1)))) {
						logerror(m, ONGERROR_MEMORYFAILURE, m->src);
						return false;
					}
					if (l) {
						if (mode == 2)
							src_copy(s, (char *) pv, l);
						else
							memcpy(s, pv, l);
					}
					s[l] = 0;
					m->rv.m_len--; // remove null terminator
				}
				break;
			}
			default:
				assert(0);
			}

			step.pop();
			if (step)
				continue;
		}
		case 3: { // step

			step.pop();
			if (step) {
				assert(0);
				continue;
			}

			if (!src_skip(m))
				return true;

			if (m->par) {
				while (m->par && *m->src == m->par) {
					m->par--;
					m->src++;
					if (!src_skip(m))
						return true;
				}
				if (!m->par)
					return true;
			}
			// operators
			int opr, n;
			if (!(n = src_op(m->src, &opr))) {
				if (*m->src == ',' && m->sq.peek() == BRAVOSCRIPT_VAR) {
					m->src++;
					continue;
				}
				return true;
			}

			switch (opr) {
			case OP_SUB_SUB:
			case OP_ADD_ADD: {
				if (m->out != ONGNODE_NUMBER || m->dis == ONGREADONLY) {
					logerror(m, ONGERROR_SYNTAXERROR, m->src);
					return false;
				}

				if (opr == OP_SUB_SUB)
					*((int *) m->pobj) -= 1;
				else
					*((int *) m->pobj) += 1;
				m->src += 2;
				step.push(3);
				continue;
			}
			case OP_SUB_EQU:
			case OP_ADD_EQU:
			case OP_MUL_EQU:
			case OP_DIV_EQU:
			case OP_OR_EQU:
			case OP_AND_EQU:
			case OP_MOD_EQU:
			case OP_XOR_EQU:
			case OP_SHL_EQU:
			case OP_SHR_EQU: {
				((char *) &opr)[3 - n] = 0;
				op = opr;
				od = OP_INI;

				if (m->dis == ONGREADONLY) {

					logerror(m, ONGERROR_SYNTAXERROR, m->src);
					return false;
				}
				tm = m;
				tm.par = 0;
				tm.var = m->hvar;
				tm.sq += 0;
				tm.src += n;
				if (!eval(&tm))
					return false;

				m->src = tm.src;
				m->ln = tm.ln;
				tm.out = m->out; // save use in step 4
				m->out = tm.rv.m_type;
				tm.dis = m->dis; // save use in step 4
				m->dis = ONGREADONLY;
				l = tm.rv.m_len;
				if (m->out == ONGNODE_STRING) {
					pv   = tm.rv.m_data;
					mode = 1;
				} else {
					iv   = tm.rv;
					mode = 0;
				}
				tm.pobj = m->pobj; // save use in step 4
				tm.hvar = m->hvar; // save use in step 4
				m->pobj = pv;
				step.push(3, 4, 2, 0);
				continue;
			}
			case OP_AND:
			case OP_SUB:
			case OP_ADD:
			case OP_MUL:
			case OP_MOD:
			case OP_DIV:
			case OP_OR:
			case OP_XOR:
			case OP_SHL:
			case OP_SHR: {
				op = opr;
				od = OP_INI;
				m->src++;
				break;
			}
			case OP_EQU:
			case OP_COL: {
				// clone context
				tm = m;
				tm.par = 0;
				tm.var = m->hvar;
				tm.sq += 0;
				tm.src++;
				if (!src_skip(&tm) || m->dis == ONGREADONLY) {
					logerror(&tm, ONGERROR_SYNTAXERROR, tm.src);
					return false;
				}

				switch (*tm.src) {
				case '[': {// x = [...]

					tm.src++;
					if (m->dis != ONGVARIABLE) {
						logerror(&tm, ONGERROR_SYNTAXERROR, tm.src);
						return false;
					}

					if (!parse_array(&tm, m->hvar))
						return false;

					m->out = ONGNODE_VAR;
					pv     = m->hvar;
					m->dis = ONGVARIABLE;
					m->pobj= m->hvar->m_pData;
					break;
				}
				case '{': {// x = {...}

					if (m->dis != ONGVARIABLE) {

						logerror(&tm, ONGERROR_SYNTAXERROR, tm.src);
						return false;
					}
					tm.src++;
					tm.loc = m->hvar;
					tm.sq += BRAVOSCRIPT_VAR;
					tm.loc->Type = ONGNODE_OBJECT;
					if (!eval(&tm))
						return false;

					tm.sq--;

					if (*tm.src++ != '}') {
						logerror(&tm, ONGERROR_SYNTAXERRORMISSING, tm.src, '}');
						return false;
					}
					m->out = ONGNODE_VAR;
					pv     = m->hvar;
					m->dis = ONGVARIABLE;
					m->pobj= m->hvar->m_pData;
					break;
				}
				default: { // x = ...
					if (!eval(&tm))
						return false;

					if (m->dis == ONGVARIABLE) {
						if (!m_Memory.setNode(m->hvar, tm.rv.m_data,
								tm.rv.m_len, tm.rv.m_type)) {
							logerror(m, ONGERROR_STORAGEFAILURE, m->src);
							return false;
						}
					} else {
						switch (m->out) {
						case ONGNODE_NUMBER: {

							*((int *) m->pobj) = tm.rv;
							break;
						}
						default: {
							logerror(m, ONGERROR_STORAGEFAILURE, m->src);
							return false;
						}
						}
					}
					m->out = tm.rv.m_type;
					m->dis = ONGREADONLY;
					l = tm.rv.m_len;
					if (m->out == ONGNODE_STRING) {
						pv      = tm.rv.m_data;
						m->pobj = pv;
						mode    = 1;
					} else {
						iv      = tm.rv;
						m->iobj = iv;
						mode    = 0;
					}
				}
				}
				m->src = tm.src;
				m->ln  = tm.ln;
				step.push(2);
				continue;
			}
			default: {
				// OP_??
				return true;
			}
			}
			step.push(1);
			break;
		}
		case 4: {// step
			if (tm.dis == ONGVARIABLE) {
				if (!m_Memory.setNode(tm.hvar, m->rv.m_data, m->rv.m_len,
						m->rv.m_type)) {

					logerror(m, ONGERROR_STORAGEFAILURE, m->src);
					return false;
				}
			} else {
				if (tm.dis != ONGWRITABLE || tm.out != ONGNODE_NUMBER) {

					logerror(m, ONGERROR_STORAGEFAILURE, m->src);
					return false;
				}
				*((int *)tm.pobj) = m->rv;
			}
			step.pop();
			break;
		}
		}
	}
	assert(0);
}

////////////////////////////////////////////////////////////////////////
//
//bravoScript::parse_arg()
//
////////////////////////////////////////////////////////////////////////
bool bravoScript::parse_arg(BRAVOSCRIPTCONTEXT * m, int min, int max) {
	int i;
	cbuf b;
	carg *a, *e;
	void **p[15];
	void *d;

	if(!src_skip(m)){

		logerror(m, ONGERROR_SYNTAXERRORMISSING, m->src, (char)m->par);
		return false;
	}

	if (m->par == *m->src) {
		m->par--;
		m->src++;
		m->out = 0;
		if (min) {
			logerror(m, ONGERROR_INVALIDFUNCTIONPARAMETERS, m->src, 0);
			return false;
		}
		return true;
	}

	if (max) {
		// create arguments
		if (!b.add(NULL, sizeof(carg) * max, ((void **) &a))) {
			logerror(m, ONGERROR_MEMORYFAILURE, m->src);
			return false;
		}
	}
	m->sq += 0;
	for (i = 0;; i++) {

		if (!m->par)
			break;

		if (!eval(m))
			return false;

		if ( i < max ) {
			// skip if already max
			e = &a[i];
			e->m_type = m->rv.m_type;
			e->m_len = m->rv.m_len;
			switch (m->rv.m_type) {
			case ONGNODE_STRING:
				m->rv.m_len++;
			case ONGNODE_NUMBER:
			case BRAVONODE_BOOLEAN:
			case ONGNODE_FUNCTION:
			case ONGNODE_VAR:
				d = b.add(m->rv.m_data, m->rv.m_len, (void **) (void *)&p[i]);
				break;
			default: {
				printf("ARG TYPE: %i: %s", m->rv.m_type, m->src);
				logerror(m, ONGERROR_MEMORYFAILURE, m->src);
				assert(0);
				return false;
			}
			}
			if ( !d ) {
				logerror(m, ONGERROR_MEMORYFAILURE, m->src);
				return false;
			}
		}
		if (m->par) {
			if (*m->src != ',') {
				puts(m->src);
				logerror(m, ONGERROR_SYNTAXERROR, m->src);
				return false;
			}
			// more arguments
			m->src++;
		}
	}

	if (i < min || i > max) {

		logerror(m, ONGERROR_INVALIDFUNCTIONPARAMETERS, m->src, i);
		return false;
	}

	m->out = i;
	if (i) {
		for (i = 0; i < m->out; i++) {
			a[i].m_data = p[i];
		}
		m->pobj = b.commit();
		m->list = a;
	}
	return true;
}

////////////////////////////////////////////////////////////////////////
//
//
//
////////////////////////////////////////////////////////////////////////
bool bravoScript::parse_index(BRAVOSCRIPTCONTEXT *m) {
	char *s;
	int i, out, id;
	OngNode *node;

	id = m->id;
	out = ONGNODE_VAR;
	node = m->hvar;
	for (;;) {
		if (!parse_arg(m, 1, 1))
			return false;

		switch (m->argv[0].m_type) {
		case ONGNODE_NUMBER: {
			i = m->argv[0];
			s = NULL;
			i++; // indexes are 1 base

			if (id == BRAVOSCRIPT_ID_ARGUMENTS && i > m->np) {
				// argument index is invalid
				out = ONGNODE_UNDEFINED;
			}
			break;
		}
		case ONGNODE_STRING: {
			s = m->argv[0];
			i = m->argv[0].m_len;
			if (id == BRAVOSCRIPT_ID_ARGUMENTS) {
				// only number base indexes are allowed
				out = ONGNODE_UNDEFINED;
			}
			break;
		}
		default: {
			logerror(m, ONGERROR_SYNTAXERROR, m->src);
			free(m->pobj);
			return false;
		}
		}

		if (out != ONGNODE_VAR || !node->haveChildren ||
				!m_Memory.getNode(node, s, i, &node)) {

			while (out == ONGNODE_VAR) {
				int l;
				if (node->isObject() && ((l = src_opt(m->src)) == OP_EQU ||
						l == OP_COL)) {
					// create missing indexes
					int j;
					OngNode *pn = node;
					if (s) {
						l = i;
						j = i = 0;
					} else {
						l = 0;
						j = i;
						i = pn->Length;
					}
					for (; i <= j; i++) {
						if (!m_Memory.addNode(pn, s, l, BRAVONODE_DATASIZE, i, &node)) {

							logerror(m, ONGERROR_STORAGEFAILURE, m->src);
							free(m->pobj); // free argument
							return false;
						}
					}
					break;
				}
				out = ONGNODE_UNDEFINED;
				break;
			}
		}

		if (*m->src == '[') {

			m->par = *m->src++;

			// free argument
			free(m->pobj);
			continue;
		}
		break;
	}

	if (out == ONGNODE_VAR) {

		// free argument
		free(m->pobj);

		m->list = &VAR_IML;
		m->out = node->Type;
		m->dis = ONGVARIABLE;
		m->hvar = node;
		m->pobj = node->m_pData;
	} else if (id == BRAVOSCRIPT_ID_VAR) {

		if(!s) {

			m->ob.m_len = 0;
			m->ob.writef("%d", i-1);
			s = m->ob;
		}
		logerror(m, ONGERROR_UNDEFINEINDEX, m->src, s);

		// free argument
		free(m->pobj);
		return false;
	} else {

		// free argument
		free(m->pobj);

		m->out = ONGNODE_UNDEFINED;
		m->dis = ONGREADONLY;
		m->hvar = 0;
		m->pobj = 0;
		m->list = &DEFAULT_IML;
	}

	return true;
}

////////////////////////////////////////////////////////////////////////
//
//
//
////////////////////////////////////////////////////////////////////////
bool bravoScript::parse_array(BRAVOSCRIPTCONTEXT *m, OngNode *var) {
	int  i;
	char c;
	OngNode *node;
	BRAVOSCRIPTCONTEXT tm;

	var->Type = ONGNODE_ARRAY;
	for (i = 1;; i++) {
		// skip junks
		if (!src_skip(m)) {
			logerror(m, ONGERROR_SYNTAXERROR, m->src);
			return false;
		}

		if ( (c=*m->src) == '[' || c == '{') {
			if (!m_Memory.getNode(var, 0, i, &node)) {
				// add to array
				if (!m_Memory.addNode(var, 0, 0, BRAVONODE_DATASIZE, i, &node))
					break;
			}
			m->src++;
			if( c == '{' ) {
				// new object
				tm = m;
				tm.par = 0;
				tm.loc = node;
				tm.sq += BRAVOSCRIPT_VAR;
				tm.loc->Type = ONGNODE_OBJECT;
				if (!eval(&tm))
					return false;

				if (*tm.src++ != '}') {
					logerror(&tm, ONGERROR_SYNTAXERRORMISSING, tm.src, '}');
					return false;
				}
				m->src = tm.src;
				m->ln = tm.ln;
			} else {
				// new array
				if (!parse_array(m, node))
					return false;
			}
		} else if (c != ']') {
			tm = m;
			tm.par = 0;
			tm.sq += 0;
			if (!eval(&tm))
				return false;

			m->src = tm.src;
			m->ln = tm.ln;

			if (!m_Memory.getNode(var, 0, i, &node)) {
				// add to array
				if (!m_Memory.addNode(var, 0, 0, tm.rv.m_len, i, &node))
					break;
			}
			if (!m_Memory.setNode(node, tm.rv.m_data, tm.rv.m_len, tm.rv.m_type))
				break;
		}
		// skip junks
		src_skip(m);

		switch (*m->src++) {
		case ',':
			break;
		case ']':
			return true;
		default: {
			logerror(m, ONGERROR_SYNTAXERROR, m->src);
			return false;
		}
		}
	}

	logerror(m, ONGERROR_STORAGEFAILURE, m->src);
	return false;
}

/////////////////////////////////////////////////////////////////////////////////////////
//
//bravoScript::parse_if
//
////////////////////////////////////////////////////////////////////////////////////////
bool bravoScript::parse_if(BRAVOSCRIPTCONTEXT *m) {
	const char *p;
	bool on;
	int op, n, l;

	for (;;) {
		on = false;
		// expression 1
		if (!eval(m))
			return false;

		if (!m->rv.m_type && m->rv.m_len == 0)
			break;

		// operators if any
		if ((n = src_op(m->src, &op))) {
			switch (op) {
			case OP_EQU_EQU:
			case OP_NOT_EQU:
			case OP_GAN:
			case OP_LAN:
			case OP_GAN_EQU:
			case OP_LAN_EQU:
				m->src += n;
				break;
			default:
				op = OP_NIL;
			}
		}

		// skip junks
		if (!src_skip(m)) {
			logerror(m, ONGERROR_SYNTAXERRORMISSING, m->src, ')');
			return false;
		}

		if (op != OP_NIL) { // expression 2
			p = m->src;
			l = m->ln;
			m->lv = m->rv;
			if (!eval(m))
				return false;

			if (!m->rv.m_type && m->rv.m_len == 0)
				break;

			// test condition
			switch (op) {
			case OP_EQU_EQU:
				BRAVOSCRIPT_TESTCONDITION(==)
				break;
			case OP_NOT_EQU:
				BRAVOSCRIPT_TESTCONDITION(!=)
				break;
			case OP_GAN:
				BRAVOSCRIPT_TESTCONDITION( >)
				break;
			case OP_LAN:
				BRAVOSCRIPT_TESTCONDITION( <)
				break;
			case OP_GAN_EQU:
				BRAVOSCRIPT_TESTCONDITION(>=)
				break;
			case OP_LAN_EQU:
				BRAVOSCRIPT_TESTCONDITION(<=)
				break;
			default: {
				m->ln = l;
				logerror(m, ONGERROR_SYNTAXERROR, p);
				return false;
			}
			}
		} else {
			if (m->rv.m_type == ONGNODE_STRING) {
				on = *((char *) m->rv.m_data) ? true : false;
			} else {
				on = *((long *) m->rv.m_data) ? true : false;
			}
		}

		if (!on) {
			if (*m->src == '|' && m->src[1] == '|') {
				m->src += 2; // do next condition
			} else {
				if (src_block(m))
					break;

				m->out = false;
				return true;
			}
		} else {
			if (*m->src == '&' && m->src[1] == '&') {
				m->src += 2; // do next condition
			} else {
				if (src_block(m))
					break;

				m->out = true;
				return true;
			}
		}
	}

	logerror(m, ONGERROR_SYNTAXERROR, m->src);
	return false;
}
////////////////////////////////////////////////////////////////////////
//
//
//
////////////////////////////////////////////////////////////////////////
bool bravoScript::parse_loop(BRAVOSCRIPTCONTEXT *m, int id) {
	BRAVOSCRIPTCONTEXT tm;
	struct {
		const char *src;
		int end;
		int ln;
	} prep[3];
	memset(prep, 0, sizeof(prep));
	tm = m;
	do {
		if (id != BRAVOSCRIPT_DO) {
			tm.src++;
			if (id == BRAVOSCRIPT_FOR) { // initialization
				if (!src_skip(&tm))
					break;

				if (*tm.src != ';') {
					if (!eval(&tm))
						return false;
					// check syntax
					if (*tm.src != ';')
						break;
				}
				tm.src++; // step over ";"
				// condition;
				BRAVOSCRIPT_FETCHCONDITION(&tm,';', 0)
				// skip garbage
				if (!src_skip(&tm))
					break;

				// fetch increment
				if (*tm.src != ')') {
					prep[1].src = tm.src;
					prep[1].ln = tm.ln;
					tm.par = '(';
					if (src_skip(&tm, 0) || *tm.src != ')')
						break;
				}
				tm.src++; // step over ")"
			} else { // WHILE condition
				BRAVOSCRIPT_FETCHCONDITION(&tm, ')', '(')
			}
		}
		// statement
		if (!src_skip(&tm))
			break;

		prep[2].src = tm.src;
		prep[2].ln = tm.ln;
		if (*tm.src == '{') {
			// compound statements
			if (src_skip(&tm, 0) || *tm.src++ != '}')
				break;

		} else if (*tm.src != ';') {
			// single statement
			tm.par = 0;
			if (src_skip(&tm, 2) || *tm.src++ != ';')
				break;
		} else {
			// no statememt
			prep[2].src = 0;
			tm.src++;
		}
		// for keyword DO only
		if (id == BRAVOSCRIPT_DO) {
			int l;
			if (!src_skip(&tm)) {
				logerror(&tm, ONGERROR_SYNTAXERROR, tm.src);
				return false;
			}
			l = src_scan(tm.src);
			if (!iml_is(RW_IMLS, BRAVOSCRIPT_WHILE, tm.src, l)) {
				logerror(&tm, ONGERROR_SYNTAXERRORIDENTIFIER, tm.src, l);
				return false;
			}

			tm.src += l;
			if (*tm.src++ != '(') {
				logerror(&tm, ONGERROR_SYNTAXERROR, tm.src);
				return false;
			}
			// condition
			BRAVOSCRIPT_FETCHCONDITION(&tm, ')', '(')

			if (*tm.src++ != ';')
				break;

			m->ln = tm.ln;
			m->src = tm.src;
			goto StatementLine;
		}

		m->ln = tm.ln;
		m->src = tm.src;
		for (;;) {
			if (prep[0].src) // have condition
			{
				tm.par = prep[0].end;
				tm.src = prep[0].src;
				tm.ln = prep[0].ln;
				if (!parse_if(&tm))
					return false;
				// condition result
				if (!tm.out)
					break;
			}
			StatementLine:
			if (prep[2].src) { // have statement
				tm.level.push(id);
				if (*prep[2].src == '{') {
					tm.src = prep[2].src;
					tm.ln = prep[2].ln;
					tm.par = 0;
					if (!run(&tm))
						return false;

					if (tm.out == BRAVOSCRIPT_BREAK)
						break;

					if (tm.out == BRAVOSCRIPT_RETURN) {
						m->out = tm.out;
						m->rv  = tm.rv;
						return true;
					}
				} else {
					tm.par = 0;
					tm.src = prep[2].src;
					tm.ln = prep[2].ln;
					if (!eval(&tm))
						return false;

				}
				tm.level.pop();
			}

			if (prep[1].src) // have increment
			{
				tm.par = '(';
				tm.src = prep[1].src;
				tm.ln = prep[1].ln;
				if (!eval(&tm))
					return false;
			}
		}
		return true;

	} while (0);

	logerror(&tm, ONGERROR_SYNTAXERRORMISSING, tm.src, ';');
	return false;
}
////////////////////////////////////////////////////////////////////////
//
//
//
////////////////////////////////////////////////////////////////////////
bool bravoScript::parse_function(BRAVOSCRIPTCONTEXT *m, bool mode) {
	struct {
		const char *pName;
		unsigned char cbLength;
	} px[10];
	ONGSCRIPTFUNCTION *pcsf;
	BRAVOSCRIPTCONTEXT tm;
	OngNode *node;
	int i, t, l;
	bool cp = false;

	if (!mode) { // static function
		l = src_scan(m->src);
		if (m_Memory.getNode(m->loc, m->src, l, &node)) {
			logerror(m, ONGERROR_MULTIPLEDEFINITION, m->src, l);
			assert(0);
			return false;
		}

		px[0].pName = m->src;
		px[0].cbLength = l;
		m->src += l; // step over name
	} else {
		px[0].pName = NULL;
		px[0].cbLength = 0;
	}

	if (!src_skip(m) || *m->src != '(') {
		logerror(m, ONGERROR_SYNTAXERRORMISSING, m->src, '(');
		return false;
	}
	// arguments loop
	t = 0; // zero total
	for (i = 1; i < 10;) { // remove any garbage
		m->src++;
		if (!src_skip(m))
			break;

		if (!(l = src_scan(m->src)))
			break; // no argument

		// save argument
		px[i].pName = m->src;
		px[i].cbLength = l;
		t++;
		t += l;
		i++;
		m->src += l; // step over this argument
		if (!src_skip(m)) {
			assert(0);
			return false;
		}

		if (*m->src != ',') // is there more
			break; // no more
	}
	// parameter check 1
	if (*m->src++ != ')') {
		logerror(m, ONGERROR_SYNTAXERRORMISSING, m->src, ')');
		return false;
	}
	// parameter check 2
	if (!src_skip(m) || *m->src != '{') {
		logerror(m, ONGERROR_SYNTAXERRORMISSING, m->src, '{');
		return false;
	}
	// skip through function
	tm.par = *m->src;
	tm.src = m->src + 1;
	tm.ln = m->ln;
	if (src_skip(&tm, 0)) {
		m->ln = tm.ln;
		logerror(m, ONGERROR_SYNTAXERRORMISSING, tm.src, (char) tm.par);
		return false;
	}

	l = (tm.src - m->src) + 1; // length

	// complete function
	if (mode) { // dynamic function
		assert(m->var != NULL);

		// get storage
		if (!(node = m_Memory.getAnonymous(m->var))) {
			logerror(m, ONGERROR_STORAGEFAILURE, m->src);
			return false;
		}
		// how should it be stored
		if (node->Storage == ONGNODE_SOM) {
			t+= l;
			t++;
			cp = true; // make copy
		}
	} else {
		node = m->loc;
	}

	if (!m_Memory.addNode(node, px[0].pName, px[0].cbLength,
			sizeof(ONGSCRIPTFUNCTION) + t, 0, &node)) {
		logerror(m, ONGERROR_STORAGEFAILURE, m->src);
		return false;
	}

	node->Type = ONGNODE_FUNCTION;
	pcsf = (ONGSCRIPTFUNCTION *) node->m_pData;
	if (cp) { // copy function text
		((char *) (pcsf->pText = (char *) memcpy(((char *) pcsf)
				+ sizeof(ONGSCRIPTFUNCTION) + (t - l) - 1, m->src, l)))[l] = 0;

		m->app->m_Sources.spawn(m->src, pcsf->pText);
	} else {
		pcsf->pText = m->src;
	}

	pcsf->pvMe = pcsf;
	pcsf->bMode = mode;
	pcsf->iLine = m->ln;
	pcsf->iArguments = (i - 1);

	// store arguments
	char *arg = pcsf->argList;
	for (i = 1; i <= pcsf->iArguments; i++) {
		l = px[i].cbLength;
		*arg++ = l;
		memcpy(arg, px[i].pName, l);
		arg += l;
	}
	m->out = ONGNODE_FUNCTION;
	m->pobj = (void *) pcsf;
	m->hvar = node;
	m->dis = ONGVARIABLE;
	m->src = tm.src + 1;
	m->ln = tm.ln;
	return true;
}
////////////////////////////////////////////////////////////////////////
//
// bravoScript::call function
//
////////////////////////////////////////////////////////////////////////
bool bravoScript::call(BRAVOSCRIPTCONTEXT *m, ONGSCRIPTFUNCTION * pf) {
	int r, i;
	char *pa;
	OngNode *nf, *na;

	pa = pf->argList;
	if (!m_Memory.addNode(m->loc, ARGUMENTS_IML.name,
			ARGUMENTS_IML.nameLength, 4, 0, &nf)) {
		logerror(m, ONGERROR_STORAGEFAILURE, m->src);
		return false;
	}
	nf->Type = ONGNODE_METHOD;
	*nf = (void *)&ARGUMENTS_IML;
	// create function arguments
	for (i = 0; i < m->out; i++) {
		if (i < pf->iArguments) {
			r = *pa++;
		} else {
			r = 1;
			pa = "__N";
		}
		carg *a = &m->argv[i];
		if (!m_Memory.addNode(nf, pa, r, a->m_len, (i + 1), &na)
				|| !m_Memory.setNode(na, a->m_data, a->m_len,
						(ONGNODETYPE) a->m_type)) {
			logerror(m, ONGERROR_STORAGEFAILURE, m->src);
			if (m->out)
				free(m->pobj); // free argument space

			m_Memory.deleteNode(nf);
			return false;
		}
		pa += r; // next argument
	}

	if (m->out)
		free(m->pobj); // free argument space

	// keyword THIS ref
	if (!m_Memory.addNode(nf, "this", 4, sizeof(PTR), 0, &na)) {

		logerror(m, ONGERROR_STORAGEFAILURE, m->src);
		m_Memory.deleteNode(nf);
		return false;
	}
	na->Type = ONGNODE_VAR;
	*na = (void *)m->var;

	// make the call
	m->loc = nf; // function space
	m->np = i;
	m->ln = pf->iLine;
	m->src = pf->pText;
	m->level.push(BRAVOSCRIPT_FUNCTION);
	r = run(m);
	m_Memory.deleteNode(nf); //delete function space
	return r;
}
////////////////////////////////////////////////////////////////////////
//
// bravoScript::parse()
//
// set list to NULL before call
////////////////////////////////////////////////////////////////////////
bool bravoScript::parse(BRAVOSCRIPTCONTEXT *m) {
	BRAVOSCRIPTIML *pm;
	int l;
	OngNode *node;

	pm = &VAR_IML;
	if (!src_skip(m) || !isalpha(*m->src) || !(l = src_scan(m->src))) {
		if (!m->src || *m->src != '[') {
			logerror(m, ONGERROR_SYNTAXERROR, m->src);
			return false;
		}
		// make on array
		if (!(m->hvar = m_Memory.getAnonymous(m->var ? m->var : m->loc))
				|| !m_Memory.addNode(m->hvar, 0, 0, BRAVONODE_DATASIZE, 0, &m->hvar)) {
			logerror(m, ONGERROR_STORAGEFAILURE, m->src);
			return false;
		}
		m->src++;
		if (!parse_array(m, m->hvar))
			return false;

		l = 0;
	} else if (m->list == &VAR_IML) {
		//x.member
		if (!(m_Memory.getNode(m->hvar, m->src, l, &m->hvar))) {
			if (src_needvar(m, true)) {
				// create variable
				if (!(m_Memory.addNode(m->hvar, m->src, l, BRAVONODE_DATASIZE, 0, &m->hvar))) {
					logerror(m, ONGERROR_STORAGEFAILURE, m->src, 0);
					return false;
				}

			} else {
				if (!(pm = iml_find(PROPERTY_IMLS, m->src, l))) {
					if (src_needvar(m, false)) {
						// undefined symbol
						logerror(m, ONGERROR_UNDEFINESYMBOL, m->src, l);
						return false;
					} else {


					}
					m->out = ONGNODE_UNDEFINED;
					m->dis = ONGREADONLY;
					m->pobj = 0;
					pm = &DEFAULT_IML;
				}
			}
		}
	} else if (m->list == 0) {
		// clear data
		m->ob.m_len = 0;
		m->hvar = m->loc;
		for (;;) { // scan nodes

			if (!m_Memory.getNode(m->hvar, m->src, l, &m->hvar)) {
				if (src_needvar(m, true)) {
					// create variable
					if (!m_Memory.addNode(m->loc, m->src, l, BRAVONODE_DATASIZE, 0, &m->hvar)) {
						logerror(m, ONGERROR_STORAGEFAILURE, m->src);
						return false;
					}
				} else if (m->hvar && (m->hvar = m->hvar->parentNode())) {
					// up tree
					continue;
				} else if (!(pm = iml_find(MAIN_IMLS, m->src, l))) {
					if (src_needvar(m, false)) {
						// undefined symbol
						logerror(m, ONGERROR_UNDEFINESYMBOL, m->src, l);
						return false;
					}
					m->out = ONGNODE_UNDEFINED;
					m->dis = ONGREADONLY;
					m->pobj = 0;
					pm = &DEFAULT_IML;
				}
			}
			break;
		}
	} else {
		// x.member
		if (!(pm = iml_find((BRAVOSCRIPTIML *) m->list, m->src, l))) {
			node = NULL;
			switch (((BRAVOSCRIPTIML *) m->list)->id) {
			case BRAVOSCRIPT_ID_TUNE:
				if( m->pobj && ((bravoChannel *)m->pobj)->m_pTune )
					node = ((bravoChannel *)m->pobj)->m_pTune->m_pNode;
				break;
			case BRAVOSCRIPT_ID_PLAYLIST:
				if (m->app->pl)
					node = ((bravoPlaylist *) m->pobj)->m_pNode;
				break;
			case BRAVOSCRIPT_ID_MUSIC: {
				if( !m_Memory.getNode(m->app->loc, LIB_IMLS[11].name,
						LIB_IMLS[11].nameLength, &node) ) {
					if (m_Memory.addNode(m->app->loc, LIB_IMLS[11].name,
							LIB_IMLS[11].nameLength, 4, 0, &node)) {
						for(int i=8; i < 10; i++) {
							pm = &LIB_IMLS[i];
							if (!m_Memory.addNode(node, pm->name,
									pm->nameLength,4, 0, &m->hvar)) {
								node = NULL;
								break;
							}
							m->hvar->Type = ONGNODE_OBJECT;
						}
					}
				}
				if( !node ) {
					logerror(m, ONGERROR_STORAGEFAILURE, m->src);
				}
				break;
			}
			case BRAVOSCRIPT_ID_STATION:
				node = m_pContext->m_pApp->loc;
				break;
			}
			if (!node) {
				// undefined symbol
				logerror(m, ONGERROR_UNDEFINESYMBOL, m->src, l);
				return false;
			}
			// get property
			if (!m_Memory.getNode(node, m->src, l, &m->hvar)) {
				if (src_needvar(m, false)) {
					// create new property
					if (!m_Memory.addNode(node, m->src, l, BRAVONODE_DATASIZE, 0, &m->hvar)) {
						logerror(m, ONGERROR_STORAGEFAILURE, m->src);
						return false;
					}
					pm = &VAR_IML;
				} else {

					m->out = ONGNODE_UNDEFINED;
					m->dis = ONGREADONLY;
					m->pobj = 0;
					pm = &DEFAULT_IML;
				}
			} else {

				pm = &VAR_IML;
			}
		}
	}

	m->src += l; // step over symbol

	if (pm->id == BRAVOSCRIPT_ID_VAR) {
		if (!pm->sub) {
			// for keyword var
			if (m->sq.find(BRAVOSCRIPT_IF)) {

				logerror(m, ONGERROR_SYNTAXERROR, m->src);
				return false;
			}
			m->sq += BRAVOSCRIPT_VAR;
			return parse(m);
		}
		// do ref only if the data is need
		int op;
		if(*m->src == '.' || (( op=src_opt(m->src)) != OP_EQU && op != OP_COL) ) {
			// handle node ref
			while( m->hvar->Type == ONGNODE_VAR )
					m->hvar = (OngNode *)(void **)*m->hvar;

			if( m->hvar->Type == ONGNODE_METHOD) {
				pm = (BRAVOSCRIPTIML *) (void **)*m->hvar;
			}
		}
	}
	////////////////////////////////////////////////////////////////////////////////////////
	//
	////////////////////////////////////////////////////////////////////////////////////////
	if (*m->src == '[' || *m->src == '(') {
		BRAVOSCRIPTCONTEXT tm;
		int i;
		tm = m;
		tm.par = *tm.src++;
		tm.sq.push(0);

		switch (pm->id) {
		case BRAVOSCRIPT_ID_NONE: {

			logerror(m, ONGERROR_UNDEFINESYMBOL, m->src-l, l);
			return false;
		}
		//////////////////////////////////////////////////////////////////////
		// FUNCTION
		////////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_ID_FUNCTION: {

			return parse_function(m, true);
		}
		/////////////////////////////////////////////////////////////////////////
		// CHANNEL( index )
		////////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_ID_CHANNEL: {
			if (!parse_arg(&tm, 1, 1))
				return false;

			i = tm.argv[0];
			if (tm.argv[0].m_type != ONGNODE_NUMBER || i < 1 ||
					(i > m_pContext->m_pMixer->m_Channels.count &&
							(i != 9 || m->ch == -1))) {

				logerror(&tm, ONGERROR_INVALIDFUNCTIONPARAMETERS, tm.src, 0);
				free(tm.pobj);
				return false;
			}
			free(tm.pobj);
			if (i == 9)
				m->pobj = m_pContext->m_pMixer->m_Channels.direct[m->ch];
			else
				m->pobj = m_pContext->m_pMixer->m_Channels[i - 1];
			m->out = ONGNODE_CHANNEL;
			m->dis = ONGREADONLY;
			break;
		}
		////////////////////////////////////////////////////////////////////////
		// CHANNEL::TUNE[index]
		////////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_ID_CHANNEL_TUNE: {

			if( !((bravoChannel *) m->pobj)->m_pTune ) {

				logerror(&tm, ONGERROR_SYNTAXERROR, tm.src);
				return false;
			}
			tm.hvar = ((bravoChannel *) m->pobj)->m_pTune->m_pNode;
			tm.id = pm->id;
			if (!parse_index(&tm))
				return false;

			m->hvar = tm.hvar;
			m->dis = tm.dis;
			m->out = tm.out;
			m->pobj = tm.pobj;
			pm = (BRAVOSCRIPTIML *) tm.list;
			break;
		}
		////////////////////////////////////////////////////////////////////////
		// PLAYLIST[ index ]
		////////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_ID_PLAYLIST: {

			if ( pm->res ) {
				if(!playlist(m, &tm, pm))
					return false;
			} else {
				tm.hvar = ((bravoPlaylist *) m->app->pl)->m_pNode;
				tm.id = pm->id;
				if (!parse_index(&tm))
					return false;

				m->hvar = tm.hvar;
				m->out = tm.out;
				m->dis = tm.dis;
				m->pobj = tm.pobj;
				pm = (BRAVOSCRIPTIML *) tm.list;
			}
			break;
		}
		////////////////////////////////////////////////////////////////////////
		// VAR::?
		////////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_ID_VAR: {
			if (tm.par == ')') {
				if (m->hvar->Type != ONGNODE_FUNCTION) {
					logerror(&tm, ONGERROR_SYNTAXERROR, tm.src);
					return false;
				}

				if (!parse_arg(&tm, 0, 10))
					return false;

				ONGSCRIPTFUNCTION *pf =
						(ONGSCRIPTFUNCTION *) (void **)*m->hvar;
				tm.var = m->id ? m->hvar->parentNode() : m->hvar; // this ref
				// save
				m->src = tm.src;
				m->ln = tm.ln;
				if (!call(&tm, pf))
					return false;
				// restore
				tm.src = m->src;
				tm.ln = m->ln;
				if (tm.out == BRAVOSCRIPT_RETURN) {
					m->ob = tm.rv;
					m->dis = tm.dis;
					m->out = tm.rv.m_type;
					m->pobj = tm.rv.m_data;
				} else {
					m->out = ONGNODE_UNDEFINED;
					m->dis = ONGREADONLY;
				}
			} else {
				tm.hvar = m->hvar;
				tm.id = pm->id;
				if (!parse_index(&tm))
					return false;

				m->hvar = tm.hvar;
				m->out = tm.out;
				m->dis = tm.dis;
				m->pobj = tm.pobj;
				pm = (BRAVOSCRIPTIML *) tm.list;
			}
			break;
		}
		////////////////////////////////////////////////////////////////////////
		// FUNCTION::ARGUMENTS[index]
		////////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_ID_ARGUMENTS: {
			tm.hvar = m->loc;
			tm.id = pm->id;
			if (!parse_index(&tm))
				return false;

			m->hvar = tm.hvar;
			m->out = tm.out;
			m->pobj = tm.pobj;
			m->dis = tm.dis;
			pm = (BRAVOSCRIPTIML *) tm.list;
			break;
		}
		////////////////////////////////////////////////////////////////////////
		// OBJECT:
		////////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_ID_STRING: {
			if(!string(m, &tm, pm))
				return false;
			break;
		}
		case BRAVOSCRIPT_ID_DATE: {
			if(!date(m, &tm, pm))
				return false;
			break;
		}
		case BRAVOSCRIPT_ID_MATH: {
			if(!math(m, &tm, pm))
				return false;
			break;
		}
		case BRAVOSCRIPT_ID_STATION: {
			if(!station(m, &tm, pm))
				return false;
			break;
		}
		case BRAVOSCRIPT_ID_MUSIC: {
			if(!music(m, &tm, pm))
				return false;
			break;
		}
		////////////////////////////////////////////////////////////////////////
		// OBJECT:OPTIONS()
		////////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_ID_OPTIONS: {

			if(!m->app->pl || tm.par == ']'){

				logerror(m, ONGERROR_SYNTAXERROR, m->src);
				return false;
			}
			if (!parse_arg(&tm, 1, 2))
				return false;

			switch(tm.argv[0].m_type) {
			case ONGNODE_NUMBER: {

				i = tm.argv[0];
				break;
			}
			case ONGNODE_STRING: {

				break;
			}
			default: {

				logerror(&tm, ONGERROR_INVALIDFUNCTIONPARAMETERS, tm.src, 0);
				// free arg
				free(tm.pobj);
				return false;
			}
			}
			if ( i < 201){
				if ( (node=((bravoPlaylist *) m->app->pl)->m_pData) &&
						(node=node->getChild(i-99))){

					m->out = node->Type;
					m->dis = ONGREADONLY;
					m->pobj = m->out == ONGNODE_STRING ? node->m_pData : (void *)(int)*node;
				} else {

					m->out = ONGNODE_UNDEFINED;
					m->dis = ONGREADONLY;
					m->iobj = ONGNODE_UNDEFINED;
				}
			} else {

				i = (1 << (i - 201)); // bit number
				m->iobj = (((bravoPlaylist *) m->app->pl)->m_iFlags & i) ? TRUE : FALSE;
				m->out = ONGNODE_NUMBER;
				m->dis = ONGREADONLY;

				if( tm.out == 2 ){

					if(tm.argv[1].m_type != BRAVONODE_BOOLEAN) {
						// free arg
						free(tm.pobj);
						logerror(&tm, ONGERROR_INVALIDFUNCTIONPARAMETERS, tm.src, 0);
						return false;
					}

					if( (bool)tm.argv[1] ) {
						// set option
						((bravoPlaylist *) m->app->pl)->m_iFlags|= i;
				    } else {
						// unset option
				    	((bravoPlaylist *) m->app->pl)->m_iFlags&= ~i;
					}
				}
			}
			// free arg
			free(tm.pobj);
			break;
		}
		default:
			puts(m->src);
			assert(0);
			return false;
		}
		// code position
		if (tm.par) {
			puts(m->src);
			assert(0);
		}
		m->src = tm.src;
		m->ln = tm.ln;
	} else {

		switch (pm->id) {
		////////////////////////////////////////////////////////////////////////
		// NONE
		////////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_ID_NONE: {
			break;
		}
		case BRAVOSCRIPT_ID_PROPERTY: {
			switch ( (( BRAVOSCRIPTIML *)pm->sub)->id ) {
			case BRAVOSCRIPT_ID_NUMBER:
				m->out = ONGNODE_NUMBER;
				break;
			case BRAVOSCRIPT_ID_STRING:
				m->out = ONGNODE_STRING;
				break;
			case BRAVOSCRIPT_ID_BOOLEAN:
				m->out = BRAVONODE_BOOLEAN;
				break;
			case BRAVOSCRIPT_ID_UNDEFINED:
				m->out = ONGNODE_UNDEFINED;
				break;
			default:
				assert(0);
			}
			m->pobj = pm->res;
			m->dis = ONGREADONLY;
			break;
		}
		////////////////////////////////////////////////////////////////////////
		// VAR
		////////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_ID_VAR: {
			m->pobj = m->hvar->m_pData;
			m->out = m->hvar->Type;
			m->dis = ONGVARIABLE;
			break;
		}
		////////////////////////////////////////////////////////////////////////
		// CHANNEL
		////////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_ID_CHANNEL_STATUS: {
			m->iobj = ((bravoChannel *)m->pobj)->m_Status;
			m->out = ONGNODE_NUMBER;
			m->dis = ONGREADONLY;
			break;
		}
		case BRAVOSCRIPT_ID_CHANNEL_ELAPSED: {
			m->iobj = bytes2msec( ((bravoChannel *)m->pobj)->m_iPosition);
			m->out = ONGNODE_NUMBER;
			m->dis = ONGREADONLY;
			break;
		}
		case BRAVOSCRIPT_ID_CHANNEL_TUNE: {

			if( !((bravoChannel *)m->pobj)->m_pTune ){

				m->pobj = NULL;
				m->out  = ONGNODE_UNDEFINED;
			} else {

				m->out = ONGNODE_TUNE;
			}
			m->dis = ONGREADONLY;
			break;
		}
		case BRAVOSCRIPT_ID_CHANNEL_ID: {
            int i;
			for(i=0; i < m_pContext->m_pMixer->m_Channels.count;i++) {

				if( ((bravoChannel *)m->pobj)->m_iIndex ==
						m_pContext->m_pMixer->m_Channels.active[i]->m_iIndex) {

					m->iobj = i + 1;
					break;
				}
			}
			m->out = ONGNODE_NUMBER;
			m->dis = ONGREADONLY;
			break;
		}
		////////////////////////////////////////////////////////////////////////
		// PLAYLIST
		////////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_ID_PLAYLIST: {
			m->pobj = m->app->pl;
			m->out = ONGNODE_PLAYLIST;
			m->dis = ONGREADONLY;
			break;
		}
		case BRAVOSCRIPT_ID_PLAYLIST_PROGRAM: {
			m->pobj= (char *)((bravoPlaylist *)m->app->pl)->m_pSession->title();
			m->out = ONGNODE_STRING;
			m->dis = ONGREADONLY;
			break;
		}
		case BRAVOSCRIPT_ID_PLAYLIST_POSITION: {
			m->pobj = &((bravoPlaylist *)m->app->pl)->m_iPosition;
			m->out = ONGNODE_NUMBER;
			m->dis = ONGWRITABLE;
			break;
		}
		case BRAVOSCRIPT_ID_PLAYLIST_LENGTH: {
			m->iobj = ((bravoPlaylist *)m->app->pl)->m_iLength;
			m->out = ONGNODE_NUMBER;
			m->dis = ONGREADONLY;
			break;
		}
		case BRAVOSCRIPT_ID_PLAYLIST_ID: {
			m->iobj = ((bravoPlaylist *)m->app->pl)->m_iID;
			m->out = ONGNODE_NUMBER;
			m->dis = ONGREADONLY;
			break;
		}
		////////////////////////////////////////////////////////////////////////
		// STATION
		////////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_ID_STATION_ID: {
			m->iobj = m_pContext->m_iID;
			m->out = ONGNODE_NUMBER;
			m->dis = ONGREADONLY;
			break;
		}
		case BRAVOSCRIPT_ID_STATION_MODE: {
			m->iobj = m_pContext->m_iMode;
			m->out = ONGNODE_NUMBER;
			m->dis = ONGREADONLY;
			break;
		}
		case BRAVOSCRIPT_ID_STATION_REMOTEURL: {
			m->pobj = (char *) m_pContext->m_Remote.pURL;
			m->out = ONGNODE_STRING;
			m->dis = ONGREADONLY;
			break;
		}
		case BRAVOSCRIPT_ID_STATION_REMOTEREADY: {
			m->iobj = ((bravoPlaylist *)m->app->pl)->
					m_pSession->remote(BRAVOSESSION_REMOTEREADY);
			m->out = ONGNODE_NUMBER;
			m->dis = ONGREADONLY;
			break;
		}
		////////////////////////////////////////////////////////////////////////
		//  VAR:LENGTH
		////////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_ID_LENGTH: {
			m->iobj = m->hvar->Length;
			m->out = ONGNODE_NUMBER;
			m->dis = ONGREADONLY;
			break;
		}
		////////////////////////////////////////////////////////////////////////
		//  VAR:TYPE
		////////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_ID_TYPE: {

			m->iobj = m->hvar->Type;
			m->out = ONGNODE_NUMBER;
			m->dis = ONGREADONLY;
			break;
		}
		////////////////////////////////////////////////////////////////////////
		// FUNCTION::ARGUMENTS:LENGTH
		////////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_ID_ARGUMENTS_LENGTH: {
			m->out = ONGNODE_NUMBER;
			m->pobj = (void *) m->np;
			m->dis = ONGREADONLY;
			break;
		}
		////////////////////////////////////////////////////////////////////////
		//  OBJECT:
		////////////////////////////////////////////////////////////////////////
		case BRAVOSCRIPT_ID_ARGUMENTS:
		case BRAVOSCRIPT_ID_NUMBER:
		case BRAVOSCRIPT_ID_STRING:
		case BRAVOSCRIPT_ID_ARRAY:
		case BRAVOSCRIPT_ID_DATE:
		case BRAVOSCRIPT_ID_MATH:
		case BRAVOSCRIPT_ID_MUSIC:
		case BRAVOSCRIPT_ID_STATION:
		case BRAVOSCRIPT_ID_OPTIONS: {

			m->pobj = pm;
			m->dis = ONGREADONLY;
			m->out = ONGNODE_METHOD;
			if (!pm->sub || ((BRAVOSCRIPTIML *) pm->sub)[0].id != pm->id) {
				logerror(m, ONGERROR_SYNTAXERROR, m->src);
				return false;
			}
			break;
		}
		////////////////////////////////////////////////////////////////////////
		//
		////////////////////////////////////////////////////////////////////////
		default: {
			printf("pm_id: %d %s\n", pm->id, pm->name);
			puts(m->src);
			assert(0);
			break;
		}
		}
	}// end main if

	// object.member
	if (*m->src == '.') {
		m->src++;
		m->id = pm->id;
		if (!(m->list = pm->sub)) {
			puts(m->src);
			assert(0);
			return false;
		}
		return parse(m);
	}

	return true;
}

////////////////////////////////////////////////////////////////////////
//
//
//
////////////////////////////////////////////////////////////////////////
bool bravoScript::parse_import(BRAVOSCRIPTCONTEXT *m) {
	BRAVOSCRIPTCONTEXT tm;
	const char *p;
	int l;

	l = src_scan(m->src);
	tm.ob.writef("%s/com.", g_pAppPath);
	tm.ob.write(m->src, l);
	tm.ob.write(".ong", 5);

	p = m->src;
	m->src += l;
	if (!src_skip(m) || *m->src++ != ';') {
		assert(0);
	}

	m_Memory.addNode(m->loc, p, l, BRAVONODE_DATASIZE, 0, &tm.var);
	tm.var->Storage = ONGNODE_SOM;
	tm.var->Type = ONGNODE_OBJECT;
	tm.app = m->app;
	tm.loc = m->loc;

	if (!load(&tm, tm.ob)) {
		assert(0);
		return false;
	}
	m->app->m_Sources.discard(tm.src);
	return true;
}

////////////////////////////////////////////////////////////////////////
//
// bravoScript:load()
//
////////////////////////////////////////////////////////////////////////
bool bravoScript::load(BRAVOSCRIPTCONTEXT *m, const char *pFilename) {
	FILE *pFile;
	char *p, *s;
	unsigned int l, r;

	// open script file
	if ((pFile = fopen(pFilename, "rb")) == NULL) {
		return false;
	}
	// get script length
	fseek(pFile, 0, SEEK_END);
	l = ftell(pFile);
	rewind(pFile);

	// allocate memory for script
	if (!(p = (char *) malloc(l + 1))) {
		fclose(pFile);
		return false;
	}
	// read script in memory
	r = fread(p, 1, l, pFile);
	fclose(pFile); // done with it
	if (r != l) {
		free(p);
		return false;
	}
	p[l] = 0; // terminate source

	// clean up script
	if( *(s = src_compact(p, l)) ) {
		free(p);
		return false;
	}
	// free up unused memory
	if( l - (r=(s-p)) > 20) {
		r++; // plus terminator
		if( (s = (char *) realloc(p, r)) ) {
			p = s;
		}
	}
	// add script
	s = strdup(basename(pFilename));
	if (!(m->app->m_Sources.lock(p, s, true))) {
		free(p);
		if (s)
			free(s);
		return false;
	}
	m->src = p;
	// run script
	if (!run(m)) {

		m->app->m_Sources.unlock(p);
		return false;
	}
	m->src = p;
	m->pobj = (void *) r;
	return true;
}

////////////////////////////////////////////////////////////////////////
//
// bravoScript::music
//
///////////////////////////////////////////////////////////////////////
bool bravoScript::music(BRAVOSCRIPTCONTEXT *m, BRAVOSCRIPTCONTEXT *tm, BRAVOSCRIPTIML *pm) {

	if (!parse_arg(tm, 0, 1))
		return false;

	m->out = ONGNODE_NUMBER;

	switch ((int) pm->res) {
	case 1: {
		// sreach
		cval qry;
		int i, c, l, f = 0;
		OngNode *lib,*var1, *var2, *var3, *var4;
		const char *a;
		char *s;
		BRAVOSCRIPTIML *piml, *ml, *ppm[10];
		static const char
			q0[] = "SELECT COUNT(Tunes.ID)",
			q1[] = "SELECT Tunes.ID",
			q2[] = " FROM Tunes"
				   " INNER JOIN Artists ON Artists.ID=Tunes.ArtistID"
				   " INNER JOIN Riddims ON Riddims.ID=Tunes.RiddimID"
				   " INNER JOIN Albums ON Albums.ID=Tunes.AlbumID"
				   " WHERE ",
			cszAND[] = " AND ";

		if (tm->out) {
			f = *tm->argv;
			free(tm->pobj);
		}
		bravoRecordSet rs;
		rs.setConnection(m_pContext->m_pConnection);
		qry.alloc(512);
		ml = &LIB_IMLS[11];
		if(!m_Memory.getNode(m->app->loc, ml->name, ml->nameLength, &lib)) {
			logerror(m, ONGERROR_STORAGEFAILURE, m->src);
			return false;
		}

		if (f & 0x02) {
			qry.write(q0, sizeof(q0) - 1);
		} else {
			qry.write(q1, sizeof(q1) - 1);
			ppm[0] = (BRAVOSCRIPTIML *) LIB_IMLS[4].sub;
			c = 1;
			if ((var1 = lib->getChild(LIB_IMLS[8].name))) {
				for (i = 4; i < 8; i++) {
					piml = &LIB_IMLS[i];
					if ((var2 = var1->getChild(piml->alt)) && *((char *) *var2)) {
						qry.writef(",%s.ID", piml->res);
						ppm[c++] = piml = (BRAVOSCRIPTIML *) piml->sub;
						if (var2->Type != ONGNODE_STRING)
							continue;
						s = (char *)var2->m_pData;
						while ((ml = iml_iscan(piml + 1, &s))) {
							qry.writef(",%s.%s", ml->res, ml->sub ? ml->sub
									: ml->name);
							ppm[c++] = ml;
						}
					}
				}
			}
		}

		qry.write(q2, sizeof(q2) - 1);
		a = "";
		const char *q = f & 0x01 ? "%s%s.%s LIKE '%%%s%%'" : "%s%s.%s='%s'";
		if ((var1 = lib->getChild(LIB_IMLS[9].name))) {
			for (i = 0; i < 4; i++) {
				piml = &LIB_IMLS[i];
				if (!(var2 = var1->getChild(piml->alt)))
					continue;

				switch ((int) var2->Type) {
				case ONGNODE_STRING: {
					if (*(s = (char *) *var2)) {
						int d[3];
						if (src_date(s, var2->Length, d)) {
							if ((ml = &((BRAVOSCRIPTIML *) piml->sub)[2])) {
								qry.writef("%sRIGHT(%s.%s,%d)=%d%d%d", a,
										ml->res, ml->sub,
										(d[0] ? d[1] ? d[2] ? 8 : 6 : 4 : 0),
										d[2], d[1], d[0]);
							}
						} else {
							ml = &((BRAVOSCRIPTIML *) piml->sub)[1];
							qry.writef(q, a, ml->res, ml->name, rs.escape(s,
									var2->Length));
						}
						a = cszAND;
					}
					break;
				}
				case ONGNODE_NUMBER: {
					if ( (l = (int)*var2) ) {

						qry.writef("%s%s.ID=%i", a, piml->res, l);
						a = cszAND;
					}
					break;
				}
				}
			}
			a = (var2 = var1->getChild("limit")) ? (char *) *var2 : "";
		}
		// do query
		if (f & 0x02) {
			rs.open((char *) qry.m_data);
			m->pobj = (void *) (rs.eof() ? 0 : (int) rs(0));
			rs.close();
			return true;
		} else {
			qry.writef(" LIMIT %s", *a?a:"1000");
			rs.open((char *) qry.m_data);
		}

		// store results
		if( m_Memory.getNode(lib, LIB_IMLS[4].name, 5, &var1) ) {
			m_Memory.deleteNode(var1);
		}

		if(!m_Memory.addNode(lib, LIB_IMLS[4].name, 5, 4, 0,&var1)) {
			rs.close();
			logerror(m, ONGERROR_STORAGEFAILURE, m->src);
			return false;
		}
		var1->Type = ONGNODE_ARRAY;
		l = 1; // indexes
		while (!rs.eof()) {
			if (c == 1) { // array of tunes IDs only
				if (!m_Memory.addNode(var1, 0, 0, 6, l++, &var2))
					break;

				if (!m_Memory.setNode(var2, (char *) *rs.Fields(0), 6,
						ONGNODE_STRING))
					break;
			} else { // object with tunes
				if (!m_Memory.addNode(var1, 0, 0, 0, l++, &var2))
					break;
				int id = -1;
				for (i = 0; i < c; i++) {
					ml = ppm[i];
					if (id != ml->id) {
						if ((id = ml->id)) {
							piml = &LIB_IMLS[id];
							if (!m_Memory.addNode(var2, piml->name,
									piml->nameLength, 0, 0, &var3))
								break;
						} else {
							var3 = var2;
						}
					}
					s = *rs.Fields(i);
					f = strlen(s);
					if (!m_Memory.addNode(var3, ml->alt, ml->nameLength, f + 1,
							0, &var4))
						break;

					if (!m_Memory.setNode(var4, s, f, ONGNODE_STRING))
						break;
				}
			}
			rs.moveNext();
		}

		if (!rs.eof()) {
			rs.close();
			logerror(m, ONGERROR_STORAGEFAILURE, m->src);
			return false;
		}
		rs.close();
		m->pobj = (void *) l;
		break;
	}
	}
	return true;
}

///////////////////////////////////////////////////////////////////////
//
// bravoScript::string
//
////////////////////////////////////////////////////////////////////////
bool bravoScript::string(BRAVOSCRIPTCONTEXT *m, BRAVOSCRIPTCONTEXT *tm, BRAVOSCRIPTIML *pm) {
	ONGPXT *md, mds[] = {
			{ 1, 2, 2, { 0 } },
			{ 1, 1, 1, { 0 } },
			{ 0, 0, 3, {tolower } },
			{ 0, 0, 3, {toupper } },
			{ 1, 1, 0, { 0 } } };
	char *d, *r, *s;
	int i, n, c, l;
	OngNode *node;

	if (m->out == ONGNODE_STRING) {
		if (m->dis != ONGVARIABLE) {
			c = strlen((char *) m->pobj);
			s = (char *) m->pobj;
		} else {
			c = (int) m->hvar->Length;
			s = (char *) *m->hvar;
		}
	} else {
		// only define for strings
		logerror(tm, ONGERROR_UNDEFINESYMBOL, tm->src, 0);
		return false;
	}

	md = &mds[(int) pm->res];
	if (!parse_arg(tm, md->min, md->max))
		return false;

	carg *a = tm->argv;
	switch (md->id) {
	////////////////////////////////////////////////////////////////////////
	// SPLIT( by )
	////////////////////////////////////////////////////////////////////////
	case 0: {
		d = a[0];
		n = a[0].m_len;
		i = 1; // indexes
		assert(m->var != NULL);
		if (!(node = m_Memory.getAnonymous(m->var)) ||
				!m_Memory.addNode(node, 0, 0, BRAVONODE_DATASIZE, 0, &node)) {
			logerror(tm, ONGERROR_STORAGEFAILURE, tm->src);
			return false;
		}
		node->Type = ONGNODE_ARRAY;
		do {
			if ((r = strstr(s, d)))
				l = (r - s);
			else
				l = c;
			if (m_Memory.setNode(node, 0x01, i++, NULL, 0, s, l,
					ONGNODE_STRING, NULL)) {
				logerror(tm, ONGERROR_STORAGEFAILURE, tm->src);
				return false;
			}
			l += n;
			s += l;
			c -= l;
		} while (r);
		m->out = ONGNODE_ARRAY;
		m->dis = ONGVARIABLE;
		m->hvar = node;
		m->pobj = node->m_pData;
		break;
	}
	////////////////////////////////////////////////////////////////////////
	// INDEXOF(String)
	////////////////////////////////////////////////////////////////////////
	case 1: {
		if (a[0].m_type == ONGNODE_STRING) {
			s = (char *) *m->hvar;
			r = strstr(s, a[0]);
		} else {
			r = NULL;
		}
		m->out = ONGNODE_NUMBER;
		m->pobj = (void *) (r ? (r - s) : -1);
		m->dis = ONGREADONLY;
		break;
	}
	////////////////////////////////////////////////////////////////////////
	// SUBSTRING(start,end)
	////////////////////////////////////////////////////////////////////////
	case 2: {
		n = a[0];
		if (tm->out == 2) {
		} else {
			if (n < 0) {
				if ((l = c + n) > 0) {
					s += l;
					c = c - l;
				}
			} else if (n < c) {
				s += n;
				c -= n;
			}
		}

		if (!(m->pobj = m->ob.alloc(c + 1))) {

			logerror(tm, ONGERROR_MEMORYFAILURE, tm->src);
			return false;
		}
		d = (char *) m->pobj;
		strncpy(d, s, c);
		d[c] = 0;
		m->out = ONGNODE_STRING;
		m->dis = ONGREADONLY;
		break;
	}
	////////////////////////////////////////////////////////////////////////
	// TOUPPERCASE() | TOLOWERCASE()
	////////////////////////////////////////////////////////////////////////
	case 3: {
		c++; // + null
		if (!(m->pobj = m->ob.alloc(c))) {
			logerror(tm, ONGERROR_MEMORYFAILURE, tm->src);
			return false;
		}
		d = (char *) m->pobj;
		s = *m->hvar;
		for (; c; c--)
			*d++ = md->fna(*s++);
		m->out = ONGNODE_STRING;
		m->dis = ONGREADONLY;
		break;
	}
	}
	if (tm->out)
		free(tm->pobj); // free arguments

	return true;
}

///////////////////////////////////////////////////////////////////////
//
// bravoScript:: date
//
////////////////////////////////////////////////////////////////////////
bool bravoScript::date(BRAVOSCRIPTCONTEXT *m, BRAVOSCRIPTCONTEXT *tm, BRAVOSCRIPTIML *pm) {

	if( !pm->res ) {

		if (!parse_arg(tm, 0, 1))
			return false;

		m->out = ONGNODE_DATE;
		m->dis = ONGREADONLY;

		switch(tm->out){
		case 0:
			m->pobj = (void *) time(NULL);
			break;
		case 1:
			 if( tm->argv[0].m_type == ONGNODE_STRING){

			 }else{

				 m->iobj = tm->argv[0];
			 }
			 break;
		}

	} else {

		if (m->out != ONGNODE_DATE) {

			logerror(tm, ONGERROR_UNDEFINESYMBOL, tm->src, 0);
			return false;
		}

		if (!parse_arg(tm, 0, 0))
			return false;

		struct tm d;
		time_t n = m->dis == ONGVARIABLE ?
 				 *((time_t *)m->pobj) : (time_t)m->pobj;

		gmtime_r(&n, &d);

		m->out = ONGNODE_NUMBER;
		m->dis = ONGREADONLY;

		switch((int)pm->res){
		case 1: { // Second
			m->iobj = d.tm_sec;
			break;
		}
		case 2: { // Minute
			m->iobj = d.tm_min;
			break;
			}
		case 3: { // hour
			m->iobj = d.tm_hour;
				break;
			}
		case 4: { // day
			m->iobj = d.tm_mday;
			break;
		}
		case 5: { // Mouth
			m->iobj = d.tm_mon;
			break;
			}
		case 6: { // year
			m->iobj = d.tm_year + 1900;
			break;
		}
		case 7: { // day of week
			m->iobj = d.tm_wday;
			break;
		}
		case 8: { // time
			m->pobj = (void *)n;
			break;
		}
		}

	}
	return true;
}

///////////////////////////////////////////////////////////////////////
//
// bravoScript::math
//
////////////////////////////////////////////////////////////////////////
bool bravoScript::math(BRAVOSCRIPTCONTEXT *m, BRAVOSCRIPTCONTEXT *tm, BRAVOSCRIPTIML *pm) {

	switch ((int) pm->res) {
	///////////////////////////////////////////////////////////////////////
	// random([MIN,MAX])
	////////////////////////////////////////////////////////////////////////
	case 0: {
		if (!parse_arg(tm, 0, 2))
			return false;
		if (tm->out == 0) {
			m->pobj = (void *) ((MTRand *) m_pRand)->randInt();
		} else {
			carg *a = ((carg *) tm->list);
			if (tm->out != 2 || a[0].m_type != ONGNODE_NUMBER ||
					a[1].m_type	!= ONGNODE_NUMBER) {

				logerror(tm, ONGERROR_INVALIDFUNCTIONPARAMETERS, tm->src, tm->out);
				free(tm->pobj);
				return false;
			}
			m->pobj = (void *) ((MTRand *) m_pRand)->randInt((int) a[0], (int) a[1]);
			free(tm->pobj);
		}
		m->dis = ONGREADONLY;
		m->out = ONGNODE_NUMBER;
		break;
	}
	}
	return true;
}

///////////////////////////////////////////////////////////////////////
//
// bravoScript::station
//
////////////////////////////////////////////////////////////////////////
bool bravoScript::station(BRAVOSCRIPTCONTEXT *m, BRAVOSCRIPTCONTEXT *tm, BRAVOSCRIPTIML *pm) {
	int n = (int) pm->res ? 0 : 1;

	if (!parse_arg(tm, n, n))
		return false;

	m->out = ONGNODE_NUMBER;
	m->dis = ONGREADONLY;

	switch ((int) pm->res) {
	case 0:{ // LOG
		if (tm->argv->m_type == ONGNODE_STRING)
			puts((char *) *tm->argv);

		free(tm->pobj);
		m->out = ONGNODE_UNDEFINED;
		m->dis = ONGREADONLY;
		break;
	}
	case 1:  // getID
		m->iobj = m_pContext->m_iID;
		break;
	case 2:
		m->pobj = (char *) BRAVOSCRIPT_VERSION;
		m->out = ONGNODE_STRING;
		break;
	case 3:
		m->iobj = m_Memory.stats(5);
		break;
	case 4:
		m->iobj = m_Memory.stats(4);
		break;
	case 5:
		m->iobj = m_Memory.stats(7);
		break;
	case 6:
		m->iobj = m_pContext->m_pStreams->getListeners();
		break;
	case 7:
		m->iobj = m_pContext->m_pStreams->getPeakListeners();
		break;
	}
	return true;
}

///////////////////////////////////////////////////////////////////////
//
// bravoScript::playlist
//
////////////////////////////////////////////////////////////////////////
bool bravoScript::playlist(BRAVOSCRIPTCONTEXT *m, BRAVOSCRIPTCONTEXT *tm, BRAVOSCRIPTIML *pm) {

	bravoPlaylist *ppl = ((bravoPlaylist *) m->app->pl);

	m->dis = ONGREADONLY;
	m->out = ONGNODE_NUMBER;

	switch((int)pm->res) {
	////////////////////////////////////////////////////////////////////////
	// PLAYLIST::QUEUE(filename, index, tag, where)
	////////////////////////////////////////////////////////////////////////
	case 1: {
		if (!parse_arg(tm, 1, 4))
			 return false;
		carg *a = tm->argv;
		int   c = tm->out;
		m->iobj = ppl->queue(a[0], c > 1 ? a[1] : 0, c > 2 ? a[2] : 0, c > 3 ? a[3] : 1);
		free(tm->pobj);
		break;
	}
	////////////////////////////////////////////////////////////////////////
	// PLAYLIST::QUEUECOUNT([where])
	////////////////////////////////////////////////////////////////////////
	case 2: {
		if (!parse_arg(tm, 0, 1) )
			 return false;
		if( tm->out ) {
			m->iobj = ppl->queueCount((int)tm->argv[0]);
			free(tm->pobj);
		} else {
			m->iobj = ppl->queueCount(0) + ppl->queueCount(1);
		}
		break;
	}
	////////////////////////////////////////////////////////////////////
	// PLAYLIST::ATTACHEVENT(eventname,function)
	////////////////////////////////////////////////////////////////////
	case 3: {
		BRAVOSCRIPTIML *piml;

		if (!parse_arg(tm, 2, 2))
			 return false;

		carg *a = tm->argv;
		if (a[0].m_type != ONGNODE_STRING ||
				!(piml = iml_find(EVENTS_IMLS, a[0], a[0].m_len))) {
			logerror(tm, ONGERROR_INVALIDFUNCTIONPARAMETERS, tm->src, 1);
			free(tm->pobj);
			return false;
		}

		if (a[1].m_type != ONGNODE_FUNCTION) {
			logerror(tm, ONGERROR_INVALIDFUNCTIONPARAMETERS, tm->src, 2);
			free(tm->pobj);
			return false;
		}
		ppl->m_pEventFunctions[piml->id] = (ONGSCRIPTFUNCTION *) (PTR) a[1];
		free(tm->pobj);
		m->out = ONGNODE_UNDEFINED;
		break;
	}
	////////////////////////////////////////////////////////////////////
	// PLAYLIST::CANCEL()
	////////////////////////////////////////////////////////////////////
	case 4: {

		if (!parse_arg(tm, 0, 0))
			 return false;

		ppl->m_iFlags|=BRAVOPLAYLIST_CANCEL;
		break;
	}
	}
	return true;
}
#undef logerror
////////////////////////////////////////////////////////////////////////
//
// bravoScript::logerror
//
///////////////////////////////////////////////////////////////////////
void bravoScript::logerror(BRAVOSCRIPTCONTEXT *pm, int ec, const char * s, ...) {
	va_list arg;
	va_start(arg, s);
	int l = va_arg(arg, int);
	int col;
	const char *name, *p;

	assert( s != NULL);
	if (pm->app->m_Sources.info(s, &p, &name)) {
		col = src_col(p, s);
	} else {
		col = 0;
		name = "unknown";
	}

	if (m_Errors.add(name, ec, pm->ln + 1, col, s, l)) {
		puts(m_Errors.description());
		m_Errors.clear();
	}
	va_end(arg);
}
