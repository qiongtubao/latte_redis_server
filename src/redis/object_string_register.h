#ifndef __REDIS_OBJECT_STRING_REGISTER_H__
#define __REDIS_OBJECT_STRING_REGISTER_H__

/**
 * 注册 object_string 类型到 object_manager
 * 输入: 无
 * 返回: 0-成功, -1-失败
 */
int register_object_string_type(void);

#endif /* __REDIS_OBJECT_STRING_REGISTER_H__ */
