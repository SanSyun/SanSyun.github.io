---
layout: page
title: ABOUT
header:
  overlay_image: /images/profile-bg.jpg
  overlay_filter: 0.5
---

<div class="profile-wrapper">
  <div class="profile-header">
    <div class="profile-avatar">
      <!-- 浣犲彲浠ユ浛鎹负鑷繁鐨勫ご鍍忓浘鐗?-->
      <img src="{{ site.baseurl }}/images/avatar.jpg" alt="澶村儚" class="avatar">
    </div>
    <div class="profile-name">
      <h1>San</h1>
      <p class="tagline">Fortune favors the bold.</p>
    </div>
  </div>
<!-- 
  <div class="profile-section">
    <h2>馃帗 Background</h2>
    <ul>
      <li>UESTC (2025-    )</li>
      <li>CDUT    (2021-2024)</li>
    </ul>
  </div> -->

  <!-- <div class="profile-section">
    <h2>馃捈 宸ヤ綔缁忓巻</h2>
    <ul>
      <li>
        <strong>鍏徃鍚嶇О</strong> (2024-鑷充粖)
        <br>鑱屼綅鍚嶇О
        <br><em>涓昏鑱岃矗鍜屾垚灏?..</em>
      </li>
    </ul>
  </div> -->

  <!-- <div class="profile-section">
    <h2>馃専 涓汉椤圭洰</h2>
    <ul>
      <li>
        <strong>椤圭洰鍚嶇О</strong>
        <br>椤圭洰鎻忚堪...
        <br><a href="#">椤圭洰閾炬帴</a>
      </li>
    </ul>
  </div> -->

  <div class="profile-section">
    <h2>馃摤 CONTACT</h2>
    <div class="contact-info glass-card">
      <p>
        <i class="fas fa-envelope"></i> E-mail锛歴chen0303@163.com
      </p>
      <p>
        <i class="fab fa-github"></i> GitHub锛?a href="https://github.com/SanSyun">@SanSyun</a>
      </p>
      <!-- 娣诲姞鍏朵粬绀句氦濯掍綋閾炬帴 -->
    </div>
  </div>
</div>

<!-- 娣诲姞 Font Awesome 鍥炬爣鏀寔 -->
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.15.4/css/all.min.css">

<style>
.profile-wrapper {
  max-width: 800px;
  margin: 0 auto;
  padding: 20px 0;  /* 绉婚櫎宸﹀彸鍐呰竟璺濓紝鍙繚鐣欎笂涓嬪唴杈硅窛 */
}

.profile-header {
  display: flex;
  align-items: center;
  justify-content: flex-start;  /* 鏀逛负宸﹀榻?*/
  gap: 30px;
  margin-bottom: 40px;
  padding: 0 20px;  /* 娣诲姞涓庤仈绯绘柟寮忕浉鍚岀殑鍐呰竟璺?*/
}

.profile-avatar {
  flex-shrink: 0;
  margin-left: 0;  /* 纭繚娌℃湁棰濆鐨勫乏杈硅窛 */
}

.avatar {
  width: 150px;
  height: 150px;
  border-radius: 50%;
  border: 3px solid #fff;
  box-shadow: 0 0 15px rgba(0,0,0,0.1);
}

.profile-name {
  text-align: left;
}

.profile-name h1 {
  margin: 0 0 10px 0;
  color: #1f1f1f;
}

.tagline {
  margin: 0;
  color: #5e5e5e;
  font-style: italic;
}

.profile-section {
  margin-bottom: 30px;
  padding: 20px;
}

.profile-section h2 {
  margin-top: 0;
  color: #1f1f1f;
  padding-bottom: 10px;
  margin-bottom: 20px;
}

.glass-card {
  background: rgba(40, 40, 40, 0.7);
  backdrop-filter: blur(10px);
  -webkit-backdrop-filter: blur(10px);
  border-radius: 12px;
  padding: 1.5rem;
  border: 1px solid rgba(255, 255, 255, 0.1);
  box-shadow: 0 4px 15px rgba(0, 0, 0, 0.2);
  transition: all 0.3s ease;
}

.glass-card:hover {
  transform: translateY(-5px);
  box-shadow: 0 8px 25px rgba(0, 0, 0, 0.3);
}

.skills {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
}

.skill-tag {
  background: #f0f0f0;
  padding: 5px 15px;
  border-radius: 20px;
  font-size: 0.9em;
  color: #4d4d4d;
}

.contact-info {
  display: flex;
  flex-direction: column;
  gap: 15px;
  color: rgba(255, 255, 255, 0.9);
}

.contact-info p {
  margin: 0;
  display: flex;
  align-items: center;
  transition: transform 0.3s ease;
}

.contact-info p:hover {
  transform: translateX(5px);
}

.contact-info i {
  width: 20px;
  margin-right: 15px;
  color: rgba(255, 255, 255, 0.8);
}

/* 閾炬帴鏍峰紡 */
.contact-info a {
  color: rgba(255, 255, 255, 0.9);
  text-decoration: none;
  border-bottom: 1px dashed rgba(255, 255, 255, 0.4);
  transition: all 0.3s ease;
  padding-bottom: 2px;
}

.contact-info a:hover {
  color: #fff;
  border-bottom-color: #fff;
}

/* 淇濇寔鍏朵粬閾炬帴鐨勫師濮嬫牱寮?*/
a:not(.contact-info a) {
  color: #1f1f1f;
  text-decoration: none;
  transition: color 0.3s;
}

a:not(.contact-info a):hover {
  color: #000;
}

/* 鍝嶅簲寮忚璁?*/
@media (max-width: 600px) {
  .profile-wrapper {
    padding: 10px;
  }
  
  .profile-section {
    padding: 15px;
  }
}
</style>

