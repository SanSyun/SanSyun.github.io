---
layout: page
title: 个人简介
header:
  overlay_image: /images/profile-bg.jpg
  overlay_filter: 0.5
---

<div class="profile-wrapper">
  <div class="profile-header">
    <div class="profile-avatar">
      <!-- 你可以替换为自己的头像图片 -->
      <img src="{{ site.baseurl }}/images/avatar.jpg" alt="头像" class="avatar">
    </div>
    <div class="profile-name">
      <h1>你的名字</h1>
      <p class="tagline">你的简短介绍或座右铭</p>
    </div>
  </div>

  <div class="profile-section">
    <h2>🎓 教育背景</h2>
    <ul>
      <li>学校名称 - 专业名称 (2020-2024)</li>
      <li>其他教育经历...</li>
    </ul>
  </div>

  <div class="profile-section">
    <h2>💼 工作经历</h2>
    <ul>
      <li>
        <strong>公司名称</strong> (2024-至今)
        <br>职位名称
        <br><em>主要职责和成就...</em>
      </li>
    </ul>
  </div>

  <div class="profile-section">
    <h2>🛠 技能特长</h2>
    <div class="skills">
      <span class="skill-tag">技能1</span>
      <span class="skill-tag">技能2</span>
      <span class="skill-tag">技能3</span>
      <!-- 添加更多技能标签 -->
    </div>
  </div>

  <div class="profile-section">
    <h2>🌟 个人项目</h2>
    <ul>
      <li>
        <strong>项目名称</strong>
        <br>项目描述...
        <br><a href="#">项目链接</a>
      </li>
    </ul>
  </div>

  <div class="profile-section">
    <h2>📬 联系方式</h2>
    <div class="contact-info">
      <p>
        <i class="fas fa-envelope"></i> 邮箱：your.email@example.com
      </p>
      <p>
        <i class="fab fa-github"></i> GitHub：<a href="https://github.com/yourusername">@yourusername</a>
      </p>
      <!-- 添加其他社交媒体链接 -->
    </div>
  </div>
</div>

<!-- 添加 Font Awesome 图标支持 -->
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.15.4/css/all.min.css">

<style>
.profile-wrapper {
  max-width: 800px;
  margin: 0 auto;
  padding: 20px;
}

.profile-header {
  text-align: center;
  margin-bottom: 40px;
}

.profile-avatar {
  margin-bottom: 20px;
}

.avatar {
  width: 150px;
  height: 150px;
  border-radius: 50%;
  border: 3px solid #fff;
  box-shadow: 0 0 15px rgba(0,0,0,0.1);
}

.profile-name h1 {
  margin: 0;
  color: #1f1f1f;
}

.tagline {
  color: #5e5e5e;
  font-style: italic;
}

.profile-section {
  margin-bottom: 30px;
  padding: 20px;
  background: #fff;
  border-radius: 10px;
  box-shadow: 0 2px 10px rgba(0,0,0,0.05);
}

.profile-section h2 {
  margin-top: 0;
  color: #1f1f1f;
  border-bottom: 2px solid #ececec;
  padding-bottom: 10px;
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
  gap: 10px;
}

.contact-info p {
  margin: 0;
}

.contact-info i {
  width: 20px;
  margin-right: 10px;
  color: #1f1f1f;
}

/* 链接样式 */
a {
  color: #1f1f1f;
  text-decoration: none;
  transition: color 0.3s;
}

a:hover {
  color: #000;
}

/* 响应式设计 */
@media (max-width: 600px) {
  .profile-wrapper {
    padding: 10px;
  }
  
  .profile-section {
    padding: 15px;
  }
}
</style>
