-- 可重复执行的答辩演示数据。价格单位为分，所有 INSERT 均尽量使用唯一键避免重复。
PRAGMA foreign_keys = ON;

-- 演示账号：密码沿用 user1 的演示密码，便于现场登录。
INSERT OR IGNORE INTO users(username,password,real_name,phone,level,role,points,invite_code)
VALUES
 ('vip_demo','ac8912e5056a6eea6e957e6f5c272b28218412de3757ea4305317b3f3edff820','VIP演示用户','13800000004',2,'customer',860,'VIPDEMO'),
 ('new_demo','c93e1941fc9e4ecefbd8fef2718622a9a8ad541955c1b7fe00214620ebc24f3a','新客演示用户','13800000005',1,'customer',120,'NEWDEMO');

INSERT OR IGNORE INTO addresses(user_id,receiver_name,phone,detail,is_default)
SELECT id,'VIP演示用户','13800000004','长春市南关区人民大街1001号',1 FROM users WHERE username='vip_demo';
INSERT OR IGNORE INTO addresses(user_id,receiver_name,phone,detail,is_default)
SELECT id,'新客演示用户','13800000005','长春市朝阳区前进大街88号',1 FROM users WHERE username='new_demo';

-- 收藏、购物车、签到、通知和客服会话，覆盖对应页面的非空状态。
INSERT OR IGNORE INTO favorites(user_id,product_id) SELECT u.id,4 FROM users u WHERE u.username='vip_demo';
INSERT OR IGNORE INTO favorites(user_id,product_id) SELECT u.id,7 FROM users u WHERE u.username='vip_demo';
INSERT OR IGNORE INTO favorites(user_id,product_id) SELECT u.id,10 FROM users u WHERE u.username='new_demo';
INSERT OR IGNORE INTO cart_items(user_id,product_id,sku_id,quantity) SELECT u.id,4,0,1 FROM users u WHERE u.username='vip_demo';
INSERT OR IGNORE INTO cart_items(user_id,product_id,sku_id,quantity) SELECT u.id,8,0,2 FROM users u WHERE u.username='new_demo';
INSERT OR IGNORE INTO user_checkins(user_id,checkin_date,points) SELECT id,'2026-11-08',10 FROM users WHERE username='vip_demo';
INSERT OR IGNORE INTO user_checkins(user_id,checkin_date,points) SELECT id,'2026-11-09',10 FROM users WHERE username='vip_demo';
INSERT OR IGNORE INTO notifications(user_id,type,content,is_read) SELECT id,'promotion','您有多张优惠券即将可用',0 FROM users WHERE username='vip_demo';
INSERT OR IGNORE INTO notifications(user_id,type,content,is_read) SELECT id,'order','订单已发货，点击查看物流',1 FROM users WHERE username='new_demo';

-- 给两个账号配券，展示“券包”和多券叠加。
INSERT OR IGNORE INTO user_coupons(user_id,promotion_id,status)
SELECT u.id,p.id,0 FROM users u JOIN promotions p ON p.id IN (2,4,5) WHERE u.username='vip_demo';
INSERT OR IGNORE INTO user_coupons(user_id,promotion_id,status)
SELECT u.id,p.id,0 FROM users u JOIN promotions p ON p.id IN (1,3,6) WHERE u.username='new_demo';

-- 订单状态覆盖：待支付、已支付、已发货、已完成、已取消。
INSERT OR IGNORE INTO orders(order_no,user_id,total_amount,discount_amount,final_amount,status,pay_time,address_receiver,address_phone,address_detail,shipping_fee,create_time)
SELECT 'DEMO-VIP-PENDING',id,129800,5000,124800,0,NULL,'VIP演示用户','13800000004','长春市南关区人民大街1001号',0,'2026-11-10 23:30:00' FROM users WHERE username='vip_demo';
INSERT OR IGNORE INTO orders(order_no,user_id,total_amount,discount_amount,final_amount,status,pay_time,address_receiver,address_phone,address_detail,shipping_fee,create_time)
SELECT 'DEMO-VIP-PAID',id,89900,10000,79900,1,'2026-11-07 09:20:00','VIP演示用户','13800000004','长春市南关区人民大街1001号',0,'2026-11-07 09:10:00' FROM users WHERE username='vip_demo';
INSERT OR IGNORE INTO orders(order_no,user_id,total_amount,discount_amount,final_amount,status,pay_time,address_receiver,address_phone,address_detail,shipping_fee,create_time)
SELECT 'DEMO-NEW-SHIPPED',id,59800,3000,56800,2,'2026-11-06 14:00:00','新客演示用户','13800000005','长春市朝阳区前进大街88号',0,'2026-11-06 13:40:00' FROM users WHERE username='new_demo';
INSERT OR IGNORE INTO orders(order_no,user_id,total_amount,discount_amount,final_amount,status,pay_time,address_receiver,address_phone,address_detail,shipping_fee,receive_time,create_time)
SELECT 'DEMO-NEW-COMPLETE',id,39900,0,39900,3,'2026-10-28 10:00:00','新客演示用户','13800000005','长春市朝阳区前进大街88号',0,'2026-10-31 16:00:00','2026-10-28 09:50:00' FROM users WHERE username='new_demo';
INSERT OR IGNORE INTO orders(order_no,user_id,total_amount,discount_amount,final_amount,status,pay_time,address_receiver,address_phone,address_detail,shipping_fee,create_time)
SELECT 'DEMO-VIP-CANCEL',id,15900,0,16700,4,'2026-10-20 12:00:00','VIP演示用户','13800000004','长春市南关区人民大街1001号',800,'2026-10-20 11:50:00' FROM users WHERE username='vip_demo';

INSERT OR IGNORE INTO order_items(order_id,product_id,product_name,price,quantity,sku_label)
SELECT o.id,p.id,p.name,p.price,1,'' FROM orders o JOIN products p ON p.id=4 WHERE o.order_no='DEMO-VIP-PENDING';
INSERT OR IGNORE INTO order_items(order_id,product_id,product_name,price,quantity,sku_label)
SELECT o.id,p.id,p.name,p.price,1,'' FROM orders o JOIN products p ON p.id=7 WHERE o.order_no='DEMO-VIP-PENDING';
INSERT OR IGNORE INTO order_items(order_id,product_id,product_name,price,quantity,sku_label)
SELECT o.id,p.id,p.name,p.price,1,'' FROM orders o JOIN products p ON p.id=4 WHERE o.order_no='DEMO-VIP-PAID';
INSERT OR IGNORE INTO order_items(order_id,product_id,product_name,price,quantity,sku_label)
SELECT o.id,p.id,p.name,p.price,1,'' FROM orders o JOIN products p ON p.id=7 WHERE o.order_no='DEMO-NEW-SHIPPED';
INSERT OR IGNORE INTO order_items(order_id,product_id,product_name,price,quantity,sku_label)
SELECT o.id,p.id,p.name,p.price,1,'' FROM orders o JOIN products p ON p.id=3 WHERE o.order_no='DEMO-NEW-COMPLETE';
INSERT OR IGNORE INTO order_items(order_id,product_id,product_name,price,quantity,sku_label)
SELECT o.id,p.id,p.name,p.price,1,'' FROM orders o JOIN products p ON p.id=14 WHERE o.order_no='DEMO-VIP-CANCEL';

INSERT OR IGNORE INTO payment_records(order_id,user_id,channel,transaction_no,amount,status)
SELECT o.id,o.user_id,'mock','TX-DEMO-VIP-PAID',o.final_amount,1 FROM orders o WHERE o.order_no='DEMO-VIP-PAID';
INSERT OR IGNORE INTO payment_records(order_id,user_id,channel,transaction_no,amount,status)
SELECT o.id,o.user_id,'mock','TX-DEMO-NEW-SHIPPED',o.final_amount,1 FROM orders o WHERE o.order_no='DEMO-NEW-SHIPPED';
INSERT OR IGNORE INTO shipments(order_id,tracking_no,status)
SELECT id,'SF-DEMO-20261106001',1 FROM orders WHERE order_no='DEMO-NEW-SHIPPED';

INSERT OR IGNORE INTO reviews(order_id,product_id,user_id,rating,content)
SELECT o.id,3,o.user_id,5,'皮质柔软，做工细致，容量也很实用。' FROM orders o WHERE o.order_no='DEMO-NEW-COMPLETE';
INSERT OR IGNORE INTO returns(order_id,user_id,reason,status,orig_status,apply_time)
SELECT o.id,o.user_id,'尺寸不合适，申请退货',0,3,'2026-11-01 10:30:00' FROM orders o WHERE o.order_no='DEMO-NEW-COMPLETE';

INSERT OR IGNORE INTO service_tickets(user_id,subject,status)
SELECT id,'咨询优惠券叠加规则','open' FROM users WHERE username='vip_demo';
INSERT OR IGNORE INTO service_messages(user_id,direction,content)
SELECT id,0,'您好，我想确认满减券和折扣券是否可以同时使用。' FROM users WHERE username='vip_demo';
INSERT OR IGNORE INTO service_messages(user_id,direction,content)
SELECT id,1,'可以，结算页勾选多张可用优惠券即可。' FROM users WHERE username='vip_demo';
-- Distribution, after-sales, and shipment demo data for existing users.
UPDATE users SET inviter_id=(SELECT id FROM users WHERE username='vip_demo')
WHERE username IN ('user1','user2');
UPDATE users SET invite_code='USER1VIP' WHERE username='user1' AND invite_code IS NULL;
UPDATE users SET invite_code='USER2VIP' WHERE username='user2' AND invite_code IS NULL;
INSERT OR IGNORE INTO commission_records(inviter_id,invitee_id,order_id,amount,status,created_at)
SELECT vip.id,o.user_id,o.id,
       CASE WHEN o.final_amount >= 10000 THEN CAST(o.final_amount * 10 / 100 AS INTEGER) ELSE 500 END,
       CASE WHEN o.status=4 THEN 2 ELSE 0 END,
       o.create_time
FROM orders o
JOIN users vip ON vip.username='vip_demo'
WHERE o.user_id IN (SELECT id FROM users WHERE username IN ('user1','user2'))
  AND o.status IN (1,2,3,4)
  AND NOT EXISTS (SELECT 1 FROM commission_records c WHERE c.order_id=o.id);

UPDATE orders SET status=5, receive_time='2026-11-08 15:30:00'
WHERE order_no='DEMO-VIP-PAID' AND status IN (1,2,3);
INSERT INTO returns(order_id,user_id,reason,status,orig_status,apply_time,audit_comment,refund_amount)
SELECT o.id,o.user_id,'商品与预期不符，申请退货',0,1,'2026-11-09 10:20:00','',0
FROM orders o
WHERE o.order_no='DEMO-VIP-PAID'
  AND NOT EXISTS (SELECT 1 FROM returns r WHERE r.order_id=o.id);

UPDATE orders SET status=2, pay_time=COALESCE(pay_time,'2026-11-09 09:00:00')
WHERE order_no='MS20261110195846440601' AND status=1;
INSERT INTO shipments(order_id,tracking_no,status,updated_at)
SELECT id,'YTO-DEMO-20261109001',1,'2026-11-09 18:30:00'
FROM orders WHERE order_no='MS20261110195846440601'
ON CONFLICT(order_id) DO UPDATE SET tracking_no=excluded.tracking_no,status=excluded.status,updated_at=excluded.updated_at;
UPDATE orders SET status=2, pay_time=COALESCE(pay_time,'2026-11-08 10:00:00')
WHERE order_no='MS20261110203143668746' AND status=1;
INSERT INTO shipments(order_id,tracking_no,status,updated_at)
SELECT id,'JD-DEMO-20261108002',2,'2026-11-10 08:15:00'
FROM orders WHERE order_no='MS20261110203143668746'
ON CONFLICT(order_id) DO UPDATE SET tracking_no=excluded.tracking_no,status=excluded.status,updated_at=excluded.updated_at;
