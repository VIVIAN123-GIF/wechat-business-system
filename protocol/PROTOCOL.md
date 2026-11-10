# 微商系统 通信协议规范 v1.0

> 依据《通信协议详细设计》冻结。格式：定长二进制头 + 变长 JSON 体。

## 1. 报文格式（网络字节序，大端）

```
+--------+--------+--------+--------+--------+--------+--------+--------+
|  Magic(4) = "MI_S"                        |  Version(4) = 1            |
+--------+--------+--------+--------+--------+--------+--------+--------+
|  Code(4) 协议码                           |  Length(4) 数据体长度       |
+--------+--------+--------+--------+--------+--------+--------+--------+
|  JSON 数据体（UTF-8，变长，Length 字节）                                |
+-------------------------------------------------------------------------+
```

- 粘包/半包处理：先读满 16 字节头，解析 `Length` 后循环 `recv` 读满数据体。

## 2. 协议码表

| 分类 | 协议码 | 名称 | 方向 | 数据体示例 |
|------|--------|------|------|------------|
| 商品 | 1001 | 查询商品列表 | C→S | `{}` |
|      | 2001 | 返回商品列表 | S→C | `{"products":[{"id":1,"name":"A","price":99.0,"stock":10}]}` |
|      | 1002 | 查询商品详情 | C→S | `{"product_id":1}` |
|      | 2002 | 返回商品详情 | S→C | `{"id":1,"name":"A","price":99.0,"stock":10,"desc":"..."}` |
| 账号 | 1006 | 登录 | C→S | `{"username":"admin","password":"<SHA256摘要>"}`（摘要=SHA256("微商系统$"||用户名||"#"||明文)） |
|      | 2006 | 登录结果 | S→C | `{"success":true,"user_id":1,"level":2,"token":"1-1694152800123-..."}` |
|      | 1007 | 注册 | C→S | `{"username":"u","password":"<SHA256摘要>"}` |
|      | 2007 | 注册结果 | S→C | `{"success":true,"user_id":3}` |
| 购物车 | 1010 | 加入购物车（库存校验） | C→S | `{"user_id":1,"token":"...","product_id":1,"quantity":2}` |
|        | 2010 | 加入购物车结果 | S→C | `{"success":true,"message":"库存充足","stock_left":13}` |
|        | 1011 | 查询购物车 | C→S | `{"user_id":1,"token":"..."}` |
|        | 2011 | 购物车明细 | S→C | `{"success":true,"items":[{"product_id":1,"sku_id":0,"price":29900,"quantity":2}]}` |
|        | 1012 | 设置购物车数量 | C→S | `{"user_id":1,"product_id":1,"sku_id":0,"quantity":3}` |
|        | 2012 | 设置数量结果 | S→C | `{"success":true,"quantity":3}` |
|        | 1013 | 删除购物车商品 | C→S | `{"user_id":1,"product_id":1,"sku_id":0}` |
|        | 2013 | 删除结果 | S→C | `{"success":true}` |
|        | 1014 | 清空购物车 | C→S | `{"user_id":1,"token":"..."}` |
|        | 2014 | 清空结果 | S→C | `{"success":true}` |
| 搜索 | 1005 | 商品搜索（名称/分类/描述模糊） | C→S | `{"keyword":"数码"}` |
|      | 2005 | 搜索结果 | S→C | `{"keyword":"数码","count":2,"products":[{"id":4,"name":"降噪无线耳机","price":899,"stock":5,"category":"数码"}]}` |
| 促销 | 1020 | 获取可用促销 | C→S | `{}` |
|      | 2020 | 返回可用促销 | S→C | `{"promotions":[{"id":1,"name":"全场9折","type":1,"param1":0.9,...}]}` |
|      |      | type=4 阶梯折扣 | S→C | `{"id":5,"name":"第2件9折","type":4,"param1":1,"param2":1.0,"param3":2,"param4":0.9}`（param1/3/5=档件数，param2/4/6=档折扣率） |
| 结算 | 1030 | 提交结算 | C→S | `{"user_id":1,"token":"...","items":[{"product_id":1,"quantity":2}],"promo_ids":[1,3]}` |
|      | 2030 | 结算结果 | S→C | `{"order_id":1,"order_no":"MS20260909001","original":598.0,"discount":88.8,"final":509.2,"status":"success"}` |
| 订单 | 1040 | 查询我的订单 | C→S | `{"user_id":1,"token":"..."}` |
|      | 2040 | 返回订单列表 | S→C | `{"orders":[{"order_id":1,"order_no":"...","total":598.0,"final":509.2,"status":1,"return_status":0,"items":[...]}]}`（return_status：0无退货 1待审核 2已通过 3已拒绝） |
| 售后 | 1050 | 申请退货 | C→S | `{"user_id":1,"token":"...","order_id":1,"reason":"质量问题"}` |
|      | 2050 | 退货申请结果 | S→C | `{"success":true,"return_id":1,"status":0,"message":"退货申请已提交，等待审核"}` |
|      | 1070 | 查询退货单（用户查自己，管理员查全部） | C→S | `{"user_id":1,"token":"..."}` |
|      | 2070 | 返回退货单列表 | S→C | `{"returns":[{"return_id":1,"order_id":1,"user_id":1,"reason":"...","status":0,"apply_time":"...","audit_time":null}]}` |
|      | 1080 | 退货审核（管理员） | C→S | `{"user_id":1,"token":"...","return_id":1,"approve":true}` |
|      | 2080 | 审核结果 | S→C | `{"success":true,"return_id":1,"status":1,"message":"退货已通过，库存已回补"}` |
|      | 1081 | 完成退货（管理员） | C→S | `{"user_id":1,"token":"...","return_id":1}` |
|      | 2081 | 退货完成结果 | S→C | `{"success":true,"return_id":1,"status":3,"message":"退货流程已完成"}` |
| 统计 | 1060 | 销售数据统计（选区间/对象） | C→S | `{"days":30,"product_id":0,"user_id":0}`（days>0 最近N天；product_id>0 限定商品；user_id>0 限定用户） |
|      | 2060 | 统计结果 | S→C | `{"summary":{"order_count":6,"total_amount":...,"discount":...,"final_amount":...,"avg_order":...,"return_count":1},"by_day":[{"date":"2026-09-16","orders":2,"amount":568.1}],"by_product":[{"product_id":1,"name":"...","quantity":5,"amount":...}],"by_user":[...]}` |
| 客服 | 1065 | 联系客服（社交消息） | C→S | `{"user_id":1,"token":"...","content":"我想退货怎么操作？"}` |
|      | 2065 | 客服应答 | S→C | `{"success":true,"received":"我想退货怎么操作？","reply":"客服小助手：已收到您的消息，我们会尽快处理（工单已登记）。"}` |
| 系统 | 1999 | 心跳/保活 | C→S | `{"timestamp":1694152800}` |
|      | 2999 | 心跳应答 | S→C | `{"timestamp":1694152800,"server_time":...}` |
| 错误 | 4001 | 通用错误 | S→C | `{"err_code":1001,"err_msg":"库存不足"}` |
|      | 4002 | 未登录/会话失效（err_code=1011） | S→C | `{"err_code":1011,"err_msg":"请先登录"}` |

## 2.1 新增业务协议（1090-1200）

以下请求均需携带登录后的 `token`，金额单位为分。

| 请求码 | 响应码 | 功能 | 请求示例 | 响应关键字段 |
|--------|--------|------|----------|--------------|
| 1090 | 2090 | 智能促销解释 | `{"user_id":2,"items":[{"product_id":1,"quantity":2}],"promo_ids":[5,2]}` | `original`、`current`、`best`、`saving_vs_current`、`lines`、`tips` |
| 1091 | 2091 | 创建拼团 | `{"user_id":2,"product_id":1,"target_count":2,"hours":24}` | `group_id`、`group_no`、`status`、`member_count`、`target_count` |
| 1092 | 2092 | 加入拼团 | `{"user_id":3,"group_id":1}` | `group_id`、`joined`、`member_count`、`target_count`、`status` |
| 1093 | 2093 | 查询可加入拼团 | `{"user_id":2}` | `groups` |
| 1095 | 2095 | 管理员流转订单状态 | `{"user_id":1,"order_id":10,"status":2}` | `order_id`、`from_status`、`status` |
| 1096 | 2096 | 查询订单审计 | `{"user_id":2,"order_id":10}` | `order_id`、`audit` |

- 1090 只进行试算，不扣库存、不创建订单。
- 拼团状态：`0` 进行中，`1` 已成团并为成员生成订单，`2` 已过期取消并回补库存。
- 拼团成团后生成 `ORDER_PENDING` 订单；创建/加入拼团时已预留库存，成员仍通过 `1100` 完成支付，支付不会重复扣库存。
- 1095 仅允许管理员执行 `已支付(1) -> 已发货(2) -> 已完成(3)`。
- 1096 允许订单本人或管理员查询操作人、动作、状态变化和时间。

| 1100 | 2100 | 模拟支付待支付订单 | `{"user_id":2,"order_id":10}` | `order_id`、`status`、`message` |
| 1101 | 2101 | 取消待支付订单 | `{"user_id":2,"order_id":10}` | `order_id`、`status`、`message` |
| 1110 | 2110 | 买家确认收货 | `{"user_id":2,"order_id":10}` | `order_id`、`status`、`message` |
| 1120 | 2120 | 查询收货地址簿 | `{"user_id":2}` | `addresses` |
| 1121 | 2121 | 新建收货地址 | `{"user_id":2,"receiver_name":"张三","phone":"13800000000","detail":"详细地址","is_default":true}` | `address_id` |
| 1130 | 2130 | 创建商品评价 | `{"user_id":2,"order_id":10,"product_id":1,"rating":5,"content":"很好"}` | `review_id` |
| 1131 | 2131 | 查询商品评价 | `{"product_id":1}` | `product_id`、`reviews` |
| 1202 | 2202 | 查询客服工单/会话 | `{"user_id":2}`（管理员返回全部） | `tickets` |
| 1203 | 2203 | 更新客服工单状态 | `{"ticket_id":1,"status":"resolved"}` | `success`、`status` |
| 1210 | 2210 | 管理促销规则 | `{"action":"list"}` / `create` / `update` / `toggle` | `promotions` 或 `success` |

- 1030 只创建 `ORDER_PENDING` 订单并保存收货地址快照，不扣库存；1100 支付时在事务中原子扣库存，任一商品库存不足则整体回滚。
- 1110 仅允许订单本人将 `ORDER_SHIPPED` 确认收货为 `ORDER_COMPLETED`。
- 1120/1121 维护用户地址簿；结算请求必须携带属于当前用户的 `address_id`，订单保存地址快照。
- 1130 仅允许本人评价已完成订单中购买过的商品，同一订单商品只能评价一次；1131 返回商品评价列表。
| 1140 | 2140 | 查询我的券包 | `{"user_id":2}` | `coupons` |
| 1141 | 2141 | 领取优惠券 | `{"user_id":2,"promotion_id":2}` | `coupon_id`、`promotion_id` |
| 1150 | 2150 | 查询消息通知 | `{"user_id":2}` | `notifications`、`unread_count` |
| 1151 | 2151 | 标记通知已读 | `{"user_id":2,"notification_id":1}` | `notification_id` |

- 商品详情返回可分享的 `ms://product/{id}` 入口；客户端生成并复制裂变文案。
- 评价可保存 `image_url` 快照；当前未接入服务端图片上传校验，因此不会仅凭客户端传入地址发放晒单积分。
| 1160 | 2160 | 查询收藏商品 | `{"user_id":2}` | `favorites` |
| 1161 | 2161 | 收藏/取消收藏 | `{"user_id":2,"product_id":1}` | `favorite` |
| 1170 | 2170 | 每日签到 | `{"user_id":2}` | `points_awarded` |
| 1180 | 2180 | 查询物流轨迹 | `{"user_id":2,"order_id":10}` | `tracking_no`、`status` |
| 1181 | 2181 | 管理员录入物流 | `{"user_id":1,"order_id":10,"tracking_no":"SF123","status":0}` | `tracking_no`、`status` |
| 1190 | 2190 | 管理员商品管理 | `{"user_id":1,"product_id":1,"action":"stock","value":20}` | `product_id`、`value` |
| 1191 | 2191 | 管理员商品列表（含已下架商品） | `{"user_id":1}` | `products`（含 `status`） |
| 1200 | 2200 | 查询商品 SKU | `{"product_id":1}` | `skus` |

- 搜索 `1005` 支持 `min_price`、`max_price`、`category`，排序支持 `price_asc`、`price_desc`、`stock_desc`。
- SKU 支持颜色、尺码、价格增量和独立库存；管理员可通过 `1190` 的 `sku_create` 操作创建规格。
- 物流状态：`0` 已揽收，`1` 运输中，`2` 派送中。

## 3. 业务错误码（置于 4001/4002 响应的 err_code 字段）

| 错误码 | 含义 |
|--------|------|
| 1001 | 库存不足 |
| 1002 | 商品不存在 |
| 1003 | 用户不存在 |
| 1004 | 购物车为空 |
| 1005 | 订单不存在 |
| 1006 | 订单状态不允许此操作 |
| 1007 | 促销活动已过期 |
| 1008 | 重复提交 |
| 1009 | 系统繁忙，请稍后重试 |
| 1010 | 网络超时 |
| 1011 | 未登录/会话失效（协议码 4002） |
| 1012 | 退货单不存在或已审核 |
| 1013 | 无权限（非管理员/非本人） |

## 4. 会话约定

- **登录鉴权**：登录（1006）成功后服务端生成会话 token（绑定 user_id）；此后**所有业务请求**（加购/结算/订单/退货/统计/客服）必须携带 `token` 字段，服务端校验 token 有效且属于请求中的 user_id，否则返回 4002。`S_ERR_UNAUTH`(4002) 仅用于鉴权失败。
- 会话 token 默认有效期为 30 分钟；过期后必须重新登录。演示种子账号不再使用 `123456` 等默认弱口令。
- **密码安全**：客户端传输与数据库存储均为 SHA-256 摘要（`SHA256("微商系统$" || username || "#" || 明文)`），全程无明文落库/上线。
- **售后闭环**：退货申请后订单置"退货中(5)"并记录原状态；管理员审核（1080）通过 → 按商品/SKU 原库存位置回补 + 订单置"已取消(4)"；管理员完成退货（1081）后退货单置"已完成(3)"，拒绝 → 恢复订单原状态。
- 结算（1030）采用**同步应答**（2030 直接返回结果）。

## 订单与优惠券状态补充

- `1030` 只创建 `ORDER_PENDING` 订单，不扣库存；金额字段 `original`、`discount`、`final` 均为整数分，并返回 `payment_status:"pending"` 与收货地址快照。
- `1030` 支持 `address_id` 和可选 `coupon_id`。结算成功后优惠券进入 `status=2`（占用中）；支付成功变为 `status=1`（已使用）。
- 优惠券状态：`0` 未使用，`2` 结算时锁定，`1` 支付成功核销；取消订单或订单超时后由 `2` 回退为 `0`。
- `1101` 仅允许订单本人取消 `ORDER_PENDING` 订单，并在同一事务中释放锁定的优惠券、记录审计日志和发送通知。
- `1100` 在事务中校验并扣减商品或 SKU 库存，随后将订单改为 `ORDER_PAID`；任一步失败都会回滚。
- `1040` 查询订单时会取消超过 30 分钟未支付的订单，并仅释放仍未被其他待支付订单占用的优惠券。
- 订单确认收货时写入 `receive_time`，退货期限从签收时间计算；后台定时任务每 30 秒清理过期拼团。
- 晒单图片目前只保存客户端提供的图片地址，不视为已验证上传，不自动发放积分。
- `2006` 登录响应包含 `points` 字段，表示用户当前积分余额。
- 注册支持 `invite_code` 绑定一级邀请人；支付成功按实付金额的 5% 写入 `commission_records`，同时生成 `payment_records`。
- 订单默认收取 8 元运费，商品金额满 99 元包邮；结算可使用 100 的整数倍积分抵扣，支付成功后积分已在订单创建事务中扣除。
- 购物车写入 `cart_items`，客服消息写入 `service_messages`，不再只保存在客户端或服务端日志中。
