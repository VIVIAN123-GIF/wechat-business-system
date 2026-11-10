// ============================================================
// unit_tests.cpp - 单元测试（纯逻辑，无需启动服务端/socket）
// 覆盖：SHA-256 已知向量、促销引擎（打折/满减/阶梯/叠加）、金额分转换
// 编译：g++ -std=c++17 -O1 -Ivendor -I. server\promotion\promotions.cpp test\unit_tests.cpp common\ProtocolEncoder.cpp -o build\unit_tests.exe
// ============================================================
#include <iostream>
#include <cmath>
#include <string>
#include "../common/SHA256.h"
#include "../server/promotion/promotions.h"

// ---------- 极简断言框架 ----------
static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do { \
    if (cond) { ++g_pass; std::cout << "[PASS] " << #cond << "\n"; } \
    else { ++g_fail; std::cout << "[FAIL] " << #cond << " (line " << __LINE__ << ")\n"; } \
} while (0)
#define CHECK_EQ_INT(actual, expected) { \
    int a_ = (actual); int e_ = (expected); \
    if (a_ == e_) { ++g_pass; std::cout << "[PASS] " << #actual << "==" << e_ << "\n"; } \
    else { ++g_fail; std::cout << "[FAIL] " << #actual << "=" << a_ << " expected " << e_ << "\n"; } \
}

// 构造一个含单件商品的订单明细（单价 cents, 数量 qty）
static std::vector<OrderItem> makeItems(int unitCents, int qty) {
    OrderItem it; it.productId = 1; it.price = unitCents; it.quantity = qty;
    return { it };
}

int main() {
    std::cout << "========== 单元测试 ==========\n";

    // ---------- 1. SHA-256 已知向量（FIPS 180-4） ----------
    std::cout << "\n--- SHA-256 ---\n";
    CHECK_EQ_INT(Sha256::hex("").compare("e3b0c44298fc1c149afbf4c8996fb924"
        "27ae41e4649b934ca495991b7852b855"), 0);
    CHECK_EQ_INT(Sha256::hex("abc").compare("ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad"), 0);
    // 课设密码摘要规则
    CHECK_EQ_INT(hashPassword("admin", "Admin#2026!").compare(
        "73f8b74dabf09b8a20de224389821f72bcb7682c10cf9be567b90523376924b1"), 0);

    // ---------- 2. 金额分转换（round 精确） ----------
    std::cout << "\n--- Money::toCents ---\n";
    CHECK_EQ_INT(Money::toCents(0.0), 0);
    CHECK_EQ_INT(Money::toCents(299.0), 29900);
    CHECK_EQ_INT(Money::toCents(269.1), 26910);   // 299*0.9=269.1 -> 26910 分
    CHECK_EQ_INT(Money::toCents(0.005), 1);       // 0.5分 round 到 1 分
    CHECK_EQ_INT(Money::toCents(0.004), 0);

    // ---------- 3. 打折促销 ----------
    std::cout << "\n--- DiscountPromotion ---\n";
    DiscountPromotion d(1, 0.9);  // 全场9折
    CHECK_EQ_INT(d.apply(10000), 9000);       // 100元 -> 90元
    CHECK_EQ_INT(d.getDiscountAmount(10000), 1000);
    CHECK_EQ_INT(d.apply(29900), 26910);      // 299元9折

    // ---------- 4. 满减券 ----------
    std::cout << "\n--- CouponPromotion ---\n";
    CouponPromotion c(2, 200.0, 30.0);  // 满200减30
    CHECK_EQ_INT(c.apply(30000), 27000);      // 300元满足 -> 270
    CHECK_EQ_INT(c.apply(15000), 15000);      // 150元不满足 -> 不变
    CHECK_EQ_INT(c.getDiscountAmount(30000), 3000);

    // ---------- 5. 直降 ----------
    std::cout << "\n--- PriceCutPromotion ---\n";
    PriceCutPromotion pc(4, 50.0);  // 直降50
    CHECK_EQ_INT(pc.apply(89900), 84900);
    CHECK_EQ_INT(pc.apply(3000), 0);          // 不足直降额 -> 0

    // ---------- 6. 阶梯折扣 ----------
    std::cout << "\n--- TierDiscountPromotion ---\n";
    // 第2件9折：规则 [(1,1.0),(2,0.9)]
    TierDiscountPromotion t9(5, {{1, 1.0}, {2, 0.9}});
    auto items2 = makeItems(29900, 2);
    CHECK_EQ_INT(t9.applyToItems(0, items2), 56810);  // 29900 + round(29900*0.9)=26910 -> 56810
    // 第3件免单：[(2,1.0),(3,0.0)]
    TierDiscountPromotion free3(6, {{2, 1.0}, {3, 0.0}});
    auto items3 = makeItems(29900, 3);
    CHECK_EQ_INT(free3.applyToItems(0, items3), 59800);  // 29900*2 + 0 = 59800

    // ---------- 7. 叠加装饰 ----------
    std::cout << "\n--- Composite + ExtraDiscount ---\n";
    // 先第2件9折 -> 56810，再满200减30 -> 53810
    auto chain = std::make_unique<CompositeDecorator>(
        std::make_unique<TierDiscountPromotion>(5, std::vector<TierDiscountPromotion::Tier>{{1,1.0},{2,0.9}}),
        std::make_unique<CouponPromotion>(2, 200.0, 30.0));
    CHECK_EQ_INT(chain->applyToItems(0, items2), 53810);

    // VIP 折上折 95折
    auto vip = std::make_unique<ExtraDiscountDecorator>(std::move(chain), 0.95);
    CHECK_EQ_INT(vip->applyToItems(0, items2), 51120);  // 53810*0.95=51119.5 -> round 51120

    // ---------- 8. 整数分无浮点误差 ----------
    std::cout << "\n--- 精度 ---\n";
    // 反复乘折扣不累积误差
    int cents = 10000;
    for (int i = 0; i < 100; ++i) cents = static_cast<int>(std::round(cents * 0.99));
    CHECK(cents > 3600 && cents < 3700);  // 不要求精确值，但不应 NaN/爆炸

    std::cout << "\n--- 促销边界参数 ---\n";
    DiscountPromotion invalidDiscount(7, -0.5);
    CHECK_EQ_INT(invalidDiscount.apply(10000), 0);
    PriceCutPromotion invalidCut(8, -50.0);
    CHECK_EQ_INT(invalidCut.apply(10000), 10000);
    CouponPromotion invalidCoupon(9, -100.0, -30.0);
    CHECK_EQ_INT(invalidCoupon.apply(10000), 10000);

    TierDiscountPromotion duplicateTiers(10, {{2, 0.9}, {2, 0.8}, {-1, 0.5}, {3, 0.0}});
    CHECK_EQ_INT(duplicateTiers.applyToItems(0, makeItems(10000, 3)), 18000);
    TierDiscountPromotion emptyTiers(11, {{0, 0.9}, {-1, -0.5}});
    CHECK_EQ_INT(emptyTiers.applyToItems(0, makeItems(10000, 2)), 20000);

    std::cout << "========== 单元测试结果: " << g_pass << " passed, "
              << g_fail << " failed ==========\n";
    return g_fail > 0 ? 1 : 0;
}
