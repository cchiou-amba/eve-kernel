// SPDX-License-Identifier: GPL-2.0-only

#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/srcu.h>

#include <linux/soc/ambarella/vout_rpc.h>

struct amb_vout_endpoint {
	const struct amb_vout_provider_ops __rcu *provider_ops;
	void __rcu *provider_priv;
	struct module __rcu *provider_owner;

	const struct amb_vout_buffer_ops __rcu *buffer_ops;
	void __rcu *buffer_priv;
	struct module __rcu *buffer_owner;

	const struct amb_vout_consumer_ops __rcu *consumer_ops;
	void __rcu *consumer_priv;
	struct module __rcu *consumer_owner;
};

static DEFINE_MUTEX(amb_vout_registry_lock);
DEFINE_STATIC_SRCU(amb_vout_registry_srcu);
static struct amb_vout_endpoint
	amb_vout_endpoints[AMB_VOUT_MAX_CONTROLLERS][AMB_VOUT_MAX_VIRTUAL_CHANNELS];

static struct amb_vout_endpoint *
amb_vout_get_endpoint(const struct amb_vout_target *target)
{
	if (!target ||
	    target->vout_id >= AMB_VOUT_MAX_CONTROLLERS ||
	    target->virtual_id >= AMB_VOUT_MAX_VIRTUAL_CHANNELS)
		return NULL;

	return &amb_vout_endpoints[target->vout_id][target->virtual_id];
}

static void amb_vout_notify_consumer(struct amb_vout_endpoint *endpoint)
{
	const struct amb_vout_consumer_ops *ops;
	struct module *owner;
	bool available;
	void *priv;
	int idx;

	idx = srcu_read_lock(&amb_vout_registry_srcu);
	ops = srcu_dereference(endpoint->consumer_ops,
			       &amb_vout_registry_srcu);
	priv = srcu_dereference(endpoint->consumer_priv,
				&amb_vout_registry_srcu);
	owner = srcu_dereference(endpoint->consumer_owner,
				 &amb_vout_registry_srcu);
	available = !!srcu_dereference(endpoint->provider_ops,
				       &amb_vout_registry_srcu);
	if (ops && ops->provider_changed &&
	    (!owner || try_module_get(owner)))
		ops->provider_changed(priv, available);
	else
		owner = NULL;
	if (owner)
		module_put(owner);
	srcu_read_unlock(&amb_vout_registry_srcu, idx);
}

int amb_vout_provider_register(const struct amb_vout_target *target,
			       const struct amb_vout_provider_ops *ops,
			       void *priv, struct module *owner)
{
	struct amb_vout_endpoint *endpoint;
	int ret = 0;

	if (!ops)
		return -EINVAL;

	endpoint = amb_vout_get_endpoint(target);
	if (!endpoint)
		return -EINVAL;

	mutex_lock(&amb_vout_registry_lock);
	if (rcu_access_pointer(endpoint->provider_ops)) {
		ret = -EBUSY;
		goto unlock;
	}

	rcu_assign_pointer(endpoint->provider_priv, priv);
	rcu_assign_pointer(endpoint->provider_owner, owner);
	rcu_assign_pointer(endpoint->provider_ops, ops);
unlock:
	mutex_unlock(&amb_vout_registry_lock);

	if (!ret)
		amb_vout_notify_consumer(endpoint);

	return ret;
}
EXPORT_SYMBOL(amb_vout_provider_register);

void amb_vout_provider_unregister(const struct amb_vout_target *target,
				  const struct amb_vout_provider_ops *ops,
				  void *priv)
{
	struct amb_vout_endpoint *endpoint;
	bool removed = false;

	endpoint = amb_vout_get_endpoint(target);
	if (!endpoint)
		return;

	mutex_lock(&amb_vout_registry_lock);
	if (rcu_access_pointer(endpoint->provider_ops) == ops &&
	    rcu_access_pointer(endpoint->provider_priv) == priv) {
		RCU_INIT_POINTER(endpoint->provider_ops, NULL);
		RCU_INIT_POINTER(endpoint->provider_priv, NULL);
		RCU_INIT_POINTER(endpoint->provider_owner, NULL);
		removed = true;
	}
	mutex_unlock(&amb_vout_registry_lock);

	if (removed) {
		synchronize_srcu(&amb_vout_registry_srcu);
		amb_vout_notify_consumer(endpoint);
	}
}
EXPORT_SYMBOL(amb_vout_provider_unregister);

int amb_vout_buffer_register(const struct amb_vout_target *target,
			     const struct amb_vout_buffer_ops *ops,
			     void *priv, struct module *owner)
{
	struct amb_vout_endpoint *endpoint;
	int ret = 0;

	if (!ops)
		return -EINVAL;

	endpoint = amb_vout_get_endpoint(target);
	if (!endpoint)
		return -EINVAL;

	mutex_lock(&amb_vout_registry_lock);
	if (rcu_access_pointer(endpoint->buffer_ops)) {
		ret = -EBUSY;
		goto unlock;
	}

	rcu_assign_pointer(endpoint->buffer_priv, priv);
	rcu_assign_pointer(endpoint->buffer_owner, owner);
	rcu_assign_pointer(endpoint->buffer_ops, ops);
unlock:
	mutex_unlock(&amb_vout_registry_lock);

	return ret;
}
EXPORT_SYMBOL(amb_vout_buffer_register);

void amb_vout_buffer_unregister(const struct amb_vout_target *target,
				const struct amb_vout_buffer_ops *ops,
				void *priv)
{
	struct amb_vout_endpoint *endpoint;
	bool removed = false;

	endpoint = amb_vout_get_endpoint(target);
	if (!endpoint)
		return;

	mutex_lock(&amb_vout_registry_lock);
	if (rcu_access_pointer(endpoint->buffer_ops) == ops &&
	    rcu_access_pointer(endpoint->buffer_priv) == priv) {
		RCU_INIT_POINTER(endpoint->buffer_ops, NULL);
		RCU_INIT_POINTER(endpoint->buffer_priv, NULL);
		RCU_INIT_POINTER(endpoint->buffer_owner, NULL);
		removed = true;
	}
	mutex_unlock(&amb_vout_registry_lock);

	if (removed)
		synchronize_srcu(&amb_vout_registry_srcu);
}
EXPORT_SYMBOL(amb_vout_buffer_unregister);

int amb_vout_consumer_register(const struct amb_vout_target *target,
			       const struct amb_vout_consumer_ops *ops,
			       void *priv, struct module *owner)
{
	struct amb_vout_endpoint *endpoint;
	int ret = 0;

	if (!ops)
		return -EINVAL;

	endpoint = amb_vout_get_endpoint(target);
	if (!endpoint)
		return -EINVAL;

	mutex_lock(&amb_vout_registry_lock);
	if (rcu_access_pointer(endpoint->consumer_ops)) {
		ret = -EBUSY;
		goto unlock;
	}

	rcu_assign_pointer(endpoint->consumer_priv, priv);
	rcu_assign_pointer(endpoint->consumer_owner, owner);
	rcu_assign_pointer(endpoint->consumer_ops, ops);
unlock:
	mutex_unlock(&amb_vout_registry_lock);

	if (!ret)
		amb_vout_notify_consumer(endpoint);

	return ret;
}
EXPORT_SYMBOL(amb_vout_consumer_register);

void amb_vout_consumer_unregister(const struct amb_vout_target *target,
				  const struct amb_vout_consumer_ops *ops,
				  void *priv)
{
	struct amb_vout_endpoint *endpoint;
	bool removed = false;

	endpoint = amb_vout_get_endpoint(target);
	if (!endpoint)
		return;

	mutex_lock(&amb_vout_registry_lock);
	if (rcu_access_pointer(endpoint->consumer_ops) == ops &&
	    rcu_access_pointer(endpoint->consumer_priv) == priv) {
		RCU_INIT_POINTER(endpoint->consumer_ops, NULL);
		RCU_INIT_POINTER(endpoint->consumer_priv, NULL);
		RCU_INIT_POINTER(endpoint->consumer_owner, NULL);
		removed = true;
	}
	mutex_unlock(&amb_vout_registry_lock);

	if (removed)
		synchronize_srcu(&amb_vout_registry_srcu);
}
EXPORT_SYMBOL(amb_vout_consumer_unregister);

#define AMB_VOUT_PROVIDER_CALL(_target, _member, ...)			\
({									\
	struct amb_vout_endpoint *__endpoint;				\
	const struct amb_vout_provider_ops *__ops;			\
	struct module *__owner;						\
	void *__priv;							\
	int __idx;							\
	int __ret = -ENODEV;						\
									\
	__endpoint = amb_vout_get_endpoint(_target);			\
	if (!__endpoint) {						\
		__ret = -EINVAL;						\
	} else {							\
		__idx = srcu_read_lock(&amb_vout_registry_srcu);		\
		__ops = srcu_dereference(__endpoint->provider_ops,	\
					 &amb_vout_registry_srcu);	\
		__priv = srcu_dereference(__endpoint->provider_priv,	\
					  &amb_vout_registry_srcu);	\
		__owner = srcu_dereference(__endpoint->provider_owner,	\
					   &amb_vout_registry_srcu);	\
		if (__ops && __ops->_member &&				\
		    (!__owner || try_module_get(__owner))) {		\
			__ret = __ops->_member(__priv, ##__VA_ARGS__);	\
			if (__owner)					\
				module_put(__owner);			\
		}							\
		srcu_read_unlock(&amb_vout_registry_srcu, __idx);	\
	}								\
	__ret;								\
})

#define AMB_VOUT_BUFFER_CALL(_target, _member, ...)			\
({									\
	struct amb_vout_endpoint *__endpoint;				\
	const struct amb_vout_buffer_ops *__ops;			\
	struct module *__owner;						\
	void *__priv;							\
	int __idx;							\
	int __ret = -ENODEV;						\
									\
	__endpoint = amb_vout_get_endpoint(_target);			\
	if (!__endpoint) {						\
		__ret = -EINVAL;						\
	} else {							\
		__idx = srcu_read_lock(&amb_vout_registry_srcu);		\
		__ops = srcu_dereference(__endpoint->buffer_ops,		\
					 &amb_vout_registry_srcu);	\
		__priv = srcu_dereference(__endpoint->buffer_priv,	\
					  &amb_vout_registry_srcu);	\
		__owner = srcu_dereference(__endpoint->buffer_owner,	\
					   &amb_vout_registry_srcu);	\
		if (__ops && __ops->_member &&				\
		    (!__owner || try_module_get(__owner))) {		\
			__ret = __ops->_member(__priv, ##__VA_ARGS__);	\
			if (__owner)					\
				module_put(__owner);			\
		}							\
		srcu_read_unlock(&amb_vout_registry_srcu, __idx);	\
	}								\
	__ret;								\
})

int amb_vout_open(const struct amb_vout_osd_config *config)
{
	if (!config)
		return -EINVAL;
	return AMB_VOUT_PROVIDER_CALL(&config->target, open, config);
}
EXPORT_SYMBOL(amb_vout_open);

int amb_vout_release(const struct amb_vout_target *target)
{
	return AMB_VOUT_PROVIDER_CALL(target, release);
}
EXPORT_SYMBOL(amb_vout_release);

int amb_vout_check_osd(const struct amb_vout_osd_config *config)
{
	if (!config)
		return -EINVAL;
	return AMB_VOUT_PROVIDER_CALL(&config->target, check_osd, config);
}
EXPORT_SYMBOL(amb_vout_check_osd);

int amb_vout_set_osd(const struct amb_vout_osd_config *config)
{
	if (!config)
		return -EINVAL;
	return AMB_VOUT_PROVIDER_CALL(&config->target, set_osd, config);
}
EXPORT_SYMBOL(amb_vout_set_osd);

int amb_vout_pan_osd(const struct amb_vout_osd_config *config)
{
	if (!config)
		return -EINVAL;
	return AMB_VOUT_PROVIDER_CALL(&config->target, pan_osd, config);
}
EXPORT_SYMBOL(amb_vout_pan_osd);

int amb_vout_setup_osd(const struct amb_vout_osd_config *config)
{
	if (!config)
		return -EINVAL;
	return AMB_VOUT_PROVIDER_CALL(&config->target, setup_osd, config);
}
EXPORT_SYMBOL(amb_vout_setup_osd);

int amb_vout_set_clut(const struct amb_vout_target *target,
		      const struct amb_vout_clut *clut)
{
	if (!clut)
		return -EINVAL;
	return AMB_VOUT_PROVIDER_CALL(target, set_clut, clut);
}
EXPORT_SYMBOL(amb_vout_set_clut);

int amb_vout_get_modes(const struct amb_vout_target *target,
		       struct amb_vout_mode *modes, u32 capacity, u32 *count)
{
	if (!modes || !capacity || !count)
		return -EINVAL;
	return AMB_VOUT_PROVIDER_CALL(target, get_modes, modes, capacity, count);
}
EXPORT_SYMBOL(amb_vout_get_modes);

int amb_vout_set_mode(const struct amb_vout_target *target,
		      const struct amb_vout_mode *mode)
{
	if (!mode)
		return -EINVAL;
	return AMB_VOUT_PROVIDER_CALL(target, set_mode, mode);
}
EXPORT_SYMBOL(amb_vout_set_mode);

int amb_vout_check_started(const struct amb_vout_target *target)
{
	return AMB_VOUT_PROVIDER_CALL(target, check_started);
}
EXPORT_SYMBOL(amb_vout_check_started);

int amb_vout_buffer_get_pool_info(const struct amb_vout_target *target,
				  struct amb_vout_buffer_pool_info *info)
{
	if (!info)
		return -EINVAL;
	return AMB_VOUT_BUFFER_CALL(target, get_pool_info, info);
}
EXPORT_SYMBOL(amb_vout_buffer_get_pool_info);

int amb_vout_buffer_acquire(const struct amb_vout_target *target,
			    u8 *slot, u64 *dma_addr)
{
	if (!slot || !dma_addr)
		return -EINVAL;
	return AMB_VOUT_BUFFER_CALL(target, acquire, slot, dma_addr);
}
EXPORT_SYMBOL(amb_vout_buffer_acquire);

int amb_vout_buffer_drop_copy(const struct amb_vout_target *target, u8 slot)
{
	return AMB_VOUT_BUFFER_CALL(target, drop_copy, slot);
}
EXPORT_SYMBOL(amb_vout_buffer_drop_copy);

int amb_vout_buffer_copy_done(const struct amb_vout_target *target, u8 slot)
{
	return AMB_VOUT_BUFFER_CALL(target, copy_done, slot);
}
EXPORT_SYMBOL(amb_vout_buffer_copy_done);

int amb_vout_buffer_pick_ready(const struct amb_vout_target *target,
			       u8 *slot, u64 *dma_addr)
{
	if (!slot || !dma_addr)
		return -EINVAL;
	return AMB_VOUT_BUFFER_CALL(target, pick_ready, slot, dma_addr);
}
EXPORT_SYMBOL(amb_vout_buffer_pick_ready);

int amb_vout_buffer_release_by_dma(const struct amb_vout_target *target,
				   u64 dma_addr,
				   struct amb_vout_buffer_release *result)
{
	if (!dma_addr || !result)
		return -EINVAL;
	return AMB_VOUT_BUFFER_CALL(target, release_by_dma, dma_addr, result);
}
EXPORT_SYMBOL(amb_vout_buffer_release_by_dma);

int amb_vout_buffer_retire_before_dma(const struct amb_vout_target *target,
				      u64 reported_dma,
				      struct amb_vout_buffer_retire *result)
{
	if (!reported_dma || !result)
		return -EINVAL;
	return AMB_VOUT_BUFFER_CALL(target, retire_before_dma, reported_dma,
				    result);
}
EXPORT_SYMBOL(amb_vout_buffer_retire_before_dma);

MODULE_DESCRIPTION("Ambarella VOUT typed RPC");
MODULE_AUTHOR("Ambarella International LLC");
MODULE_LICENSE("GPL");

