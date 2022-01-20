#define LIST_INSERT(HEAD, ENTRY)		\
do {						\
	(ENTRY)->next = (HEAD);			\
	(HEAD) = (ENTRY);			\
} while (0)

#define LIST_REMOVE(HEAD, ENTRY)		\
do {						\
	typeof(HEAD) _p;			\
	_p = (HEAD);				\
	if ((ENTRY) == _p) {			\
		(HEAD) = _p->next;		\
		break;				\
	}					\
	while (_p && (ENTRY) != _p->next)	\
		_p = _p->next;			\
	if (_p)					\
		_p->next = (ENTRY)->next;	\
} while (0)

#define LIST_FIND(HEAD, CMP, CMPARG, FOUND)	\
do {						\
	typeof(HEAD) _p;			\
	_p = (HEAD);				\
	while (_p && CMP(_p, CMPARG))		\
		_p = _p->next;			\
	*(FOUND) = _p;				\
} while (0)
