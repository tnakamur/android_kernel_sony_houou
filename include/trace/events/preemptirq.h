#ifdef CONFIG_PREEMPTIRQ_EVENTS

#undef TRACE_SYSTEM
#define TRACE_SYSTEM preemptirq

#if !defined(_TRACE_PREEMPTIRQ_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PREEMPTIRQ_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>
#include <linux/string.h>
#include <asm/sections.h>

DECLARE_EVENT_CLASS(preemptirq_template,

	TP_PROTO(unsigned long ip, unsigned long parent_ip),

	TP_ARGS(ip, parent_ip),

	TP_STRUCT__entry(
		__field(u32, caller_offs)
		__field(u32, parent_offs)
	),

	TP_fast_assign(
		__entry->caller_offs = (u32)(ip - (unsigned long)_stext);
		__entry->parent_offs = (u32)(parent_ip - (unsigned long)_stext);
	),

	TP_printk("caller=%pF parent=%pF",
		  (void *)((unsigned long)(_stext) + __entry->caller_offs),
		  (void *)((unsigned long)(_stext) + __entry->parent_offs))
);

#ifndef CONFIG_PROVE_LOCKING
DEFINE_EVENT(preemptirq_template, irq_disable,
	     TP_PROTO(unsigned long ip, unsigned long parent_ip),
	     TP_ARGS(ip, parent_ip));

DEFINE_EVENT(preemptirq_template, irq_enable,
	     TP_PROTO(unsigned long ip, unsigned long parent_ip),
	     TP_ARGS(ip, parent_ip));
#endif

#ifdef CONFIG_DEBUG_PREEMPT
DEFINE_EVENT(preemptirq_template, preempt_disable,
	     TP_PROTO(unsigned long ip, unsigned long parent_ip),
	     TP_ARGS(ip, parent_ip));

DEFINE_EVENT(preemptirq_template, preempt_enable,
	     TP_PROTO(unsigned long ip, unsigned long parent_ip),
	     TP_ARGS(ip, parent_ip));
#endif

TRACE_EVENT(irqs_disable,

	TP_PROTO(u64 delta, unsigned long caddr0, unsigned long caddr1,
				unsigned long caddr2, unsigned long caddr3),

	TP_ARGS(delta, caddr0, caddr1, caddr2, caddr3),

	TP_STRUCT__entry(
		__field(u64, delta)
		__field(void*, caddr0)
		__field(void*, caddr1)
		__field(void*, caddr2)
		__field(void*, caddr3)
	),

	TP_fast_assign(
		__entry->delta = delta;
		__entry->caddr0 = (void *)caddr0;
		__entry->caddr1 = (void *)caddr1;
		__entry->caddr2 = (void *)caddr2;
		__entry->caddr3 = (void *)caddr3;
	),

	TP_printk("delta=%llu(ns) Callers:(%pf<-%pf<-%pf<-%pf)", __entry->delta,
					__entry->caddr0, __entry->caddr1,
					__entry->caddr2, __entry->caddr3)
);

#endif /* _TRACE_PREEMPTIRQ_H */

#include <trace/define_trace.h>

#endif /* !CONFIG_PREEMPTIRQ_EVENTS */

#if !defined(CONFIG_PREEMPTIRQ_EVENTS) || defined(CONFIG_PROVE_LOCKING)
#define trace_irq_enable(...)
#define trace_irq_disable(...)
#define trace_irq_enable_rcuidle(...)
#define trace_irq_disable_rcuidle(...)
#endif

#if !defined(CONFIG_PREEMPTIRQ_EVENTS) || !defined(CONFIG_DEBUG_PREEMPT)
#define trace_preempt_enable(...)
#define trace_preempt_disable(...)
#define trace_preempt_enable_rcuidle(...)
#define trace_preempt_disable_rcuidle(...)
#endif
