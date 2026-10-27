# Асинхронна генерація CSG: практичний довідник

Цей документ призначений для Godot-проєктів, які використовують збірку engine
з цього репозиторію. Загальний опис додаткового API — в
[EXTENDED_CSG_API.md](EXTENDED_CSG_API.md), технічний план і обмеження
реалізації — в [ASYNC_CSG_PLAN.md](ASYNC_CSG_PLAN.md).

`generate_mesh_async()` — окремий opt-in API для дерева CSG-вузлів, яке ще не
додано у сцену. Воно дозволяє створити результат CSG у worker thread і отримати
`ArrayMesh`, а за потреби також `StaticBody3D` з collision.

## Життєвий цикл

1. На головному потоці створюємо orphan-дерево: кореневий `CSGShape3D` без
   батьківського вузла та його дочірні CSG-вузли.
2. Налаштовуємо форми, операції, трансформації, матеріали й modifiers.
3. Викликаємо `root.generate_mesh_async(create_static_body)`.
4. Якщо метод повернув `null`, запуск не відбувся: дерево залишається у
   власності викликача, і його треба звільнити самостійно.
5. Якщо повернуто `CSGAsyncJob`, дерево належить job. Не читаємо, не змінюємо,
   не додаємо у сцену і не звільняємо жоден його вузол.
6. Чекаємо сигнал `completed` і перевіряємо `get_status()`.
7. Job автоматично звільняє дерево на головному потоці перед сигналом: при
   успіху, помилці або скасуванні. Mesh залишається окремим `Resource`.

Job залишається активним, навіть якщо викликач втратив на нього посилання.
Проте збережіть посилання, якщо потрібно отримати результат або скасувати job.

## Приклад: Boolean-операція та фізика

```gdscript
extends Node3D

@onready var visual: MeshInstance3D = $MeshInstance3D

var terrain_body: StaticBody3D


func build_structure() -> void:
    var root := CSGCombiner3D.new()

    var box := CSGBox3D.new()
    box.size = Vector3(5.0, 5.0, 5.0)
    root.add_child(box)

    var cutter := CSGSphere3D.new()
    cutter.radius = 2.0
    cutter.operation = CSGShape3D.OPERATION_SUBTRACTION
    root.add_child(cutter)

    # На цьому етапі всі налаштування orphan-дерева мають бути завершені.
    var job := root.generate_mesh_async(true)
    if job == null:
        root.free() # Володіння не було передано.
        push_error("Не вдалося запустити генерацію CSG")
        return

    # Відтепер job володіє root і всіма його дочірніми вузлами.
    # Не використовуйте root після цього рядка, навіть після completed.
    await job.completed

    match job.get_status():
        CSGAsyncJob.STATUS_COMPLETED:
            var new_mesh := job.get_mesh()
            var new_body := job.take_static_body()

            visual.mesh = new_mesh
            if new_body != null:
                add_child(new_body)
                # MeshInstance3D і тіло мають використовувати однаковий
                # transform відносно цього Node3D.
                new_body.transform = visual.transform
                if is_instance_valid(terrain_body):
                    terrain_body.queue_free()
                terrain_body = new_body

        CSGAsyncJob.STATUS_CANCELLED:
            print("Генерацію скасовано")

        CSGAsyncJob.STATUS_FAILED:
            push_error("Помилка CSG: " + job.get_error_message())
```

Приклад не вставляє CSG-вузли у сцену. У сцену додається лише готове фізичне
тіло, а меш призначається наявному `MeshInstance3D`. Обидва результати
використовують локальні координати кореневого CSG; transform самого orphan-root
не застосовується до готового меша автоматично. Встановлюйте transform
відображення і тіла на стороні проєкту.

`StaticBody3D` містить `CollisionShape3D` з `ConcavePolygonShape3D`. Це варіант
для нерухомого ландшафту та споруд. API не створює `RigidBody3D` і не обчислює
convex decomposition.

## API `CSGAsyncJob`

Створення:

```gdscript
var job: CSGAsyncJob = root.generate_mesh_async(false)
```

`false` генерує тільки меш. `true` додатково створює static body. Потрібні
увімкнена підтримка потоків і принаймні один worker; для фізичного тіла також
має бути доступна 3D-фізика. Корінь не повинен мати батька, перебувати у сцені
або містити вузли, поставлені в чергу на видалення.

Методи й сигнал:

| API | Значення |
| --- | --- |
| `completed` | Сигнал на головному потоці після успіху, помилки або скасування. |
| `get_status()` | Поточний статус з таблиці нижче. |
| `is_finished()` | `true`, коли завершена фіналізація на головному потоці. |
| `get_mesh()` | `ArrayMesh` після успішного завершення; до нього результату немає. |
| `take_static_body()` | Забрати створене тіло у власність викликача; наступний виклик поверне `null`. |
| `get_error_message()` | Повідомлення про помилку; для скасування може бути порожнім. |
| `cancel()` | Запитати скасування job. |

Статуси:

| Константа | Значення |
| --- | --- |
| `CSGAsyncJob.STATUS_PENDING` | Генерація або фіналізація ще триває. |
| `CSGAsyncJob.STATUS_COMPLETED` | Меш успішно створений. |
| `CSGAsyncJob.STATUS_FAILED` | Згенерувати меш не вдалося; перевірте `get_error_message()`. |
| `CSGAsyncJob.STATUS_CANCELLED` | Результат відкинуто після запиту `cancel()`. |

Усі ці методи призначені для виклику з головного потоку. `completed` означає
завершення job, а не обов'язково успіх — завжди перевіряйте статус.

### Володіння результатом

- `get_mesh()` повертає `Ref<ArrayMesh>`: ресурс живе, поки на нього є
  посилання, наприклад у `MeshInstance3D`.
- `take_static_body()` передає тіло викликачу. Додайте його у сцену або
  звільніть самостійно.
- Якщо тіло не забрали, воно належить job і буде звільнене разом із ним.
- Якщо передали `false` при запуску, `take_static_body()` повертає `null`.

## Приклад: `CSGHeightMap3D` для одного чанку

```gdscript
func build_height_chunk(texture: Texture2D, pixel_region: Rect2i) -> void:
    var terrain := CSGHeightMap3D.new()
    terrain.height_map = texture
    terrain.region_enabled = true
    terrain.region_rect = pixel_region
    terrain.size = Vector3(pixel_region.size.x, 32.0, pixel_region.size.y)
    terrain.generation_mode = CSGHeightMap3D.GENERATION_DIRECT_CONTOUR_MESH
    terrain.height_steps = 16
    terrain.height_channel = CSGHeightMap3D.HEIGHT_CHANNEL_RED
    terrain.height_multiplier_channel = \
            CSGHeightMap3D.HEIGHT_MULTIPLIER_CHANNEL_GREEN

    # Матеріали, layer_settings, modifiers, bevel_settings і topology_settings
    # також призначаються тут, до generate_mesh_async().
    var job := terrain.generate_mesh_async(true)
    if job == null:
        terrain.free()
        return

    await job.completed
    if job.get_status() != CSGAsyncJob.STATUS_COMPLETED:
        if job.get_status() == CSGAsyncJob.STATUS_FAILED:
            push_warning(job.get_error_message())
        return

    # Тут проєкт призначає job.get_mesh() і job.take_static_body()
    # потрібному chunk container у правильному world transform.
```

Координати `pixel_region` — координати в спільній текстурі. `terrain.size`
визначає фізичні розміри цього регіону, а не всієї текстури. Для стикування
чанків узгодьте їх позиції, розмір пікселя, sampling та налаштування шарів.

Наявний приклад `csg_height_map_chunk_generator_3d.gd` наразі створює звичайні
CSG-вузли у сцені й **не використовує цей async API автоматично**. Для
асинхронного стримінгу потрібна окрема інтеграція з керуванням jobs.

## Скасування та застарілі результати чанків

```gdscript
var current_job: CSGAsyncJob

func cancel_build() -> void:
    if current_job != null and not current_job.is_finished():
        current_job.cancel()
```

`cancel()` не перериває Manifold-операцію посеред виконання. Job завершує
роботу, відкидає результат, звільняє дерево та подає `completed` зі статусом
`STATUS_CANCELLED`. При виході з engine незавершені jobs також завершуються
перед вимкненням залежних систем, тому складний job може затримати shutdown.

Для редагованих чанків використовуйте **revision** або generation token:

1. Кожна нова зміна чанку збільшує його revision.
2. При запуску job збережіть revision і ідентифікатор чанку в менеджері.
3. Після `completed` перевірте, що чанк ще існує і revision збігається.
4. Якщо ні — не застосовуйте старий результат. Незабране фізичне тіло
   залишиться у власності job і буде звільнене разом із ним.
5. Якщо revision актуальна — підмініть меш і collision на головному потоці.

Цей механізм не реалізований усередині `CSGAsyncJob`: менеджер чанків має
сам контролювати актуальність результату й кількість одночасних jobs.

## Потоки, ресурси й продуктивність

Відповідальність розробника — не звертатися одночасно до переданого дерева та
не змінювати ресурси, які використовує worker: materials, налаштування шарів,
modifiers та інші спільні ресурси. Сам факт, що вузол orphan, не робить його
або пов'язані ресурси автоматично thread-safe.

Перед запуском worker API на головному потоці:

- зчитує image для `CSGHeightMap3D`;
- готує локальний brush для `CSGMesh3D`, де доступ до mesh може вимагати
  renderer readback.

Це не створює друге CSG-дерево, але читання великої текстури чи меша може
викликати затримку та споживати пам'ять. У межах одного job однакова текстура
зчитується один раз; **різні jobs наразі зчитують її окремо**.

Boolean та інша побудова CSG виконуються у worker. Завершальне створення
`ArrayMesh`, optional collision, видалення дерева і сигнал відбуваються на
головному потоці. Для великої кількості чанків обмежуйте кількість активних
jobs і застосованих результатів та вимірюйте затримки кожного етапу.

Native modifiers використовують наявний CSG pipeline. Script modifiers можуть
виконати довільний GDScript у worker: не звертайтесь із них до активного scene
tree і не робіть синхронного очікування головного потоку або renderer. Те саме
стосується custom resource implementations, які можуть виконувати довільний
код. Безпечність такого коду цей API автоматично не перевіряє.

Вихідний `ArrayMesh` використовує звичайний шлях формування CSG-меша: UV,
materials, vertex color і `CUSTOM0`–`CUSTOM3` передаються через нього. Face
semantics та інші внутрішні metadata не стають автоматично окремими даними
`ArrayMesh`; якщо вони потрібні шейдеру, запишіть потрібні значення у канали
через modifiers до запуску.

## Що перевірити в проєкті

- Зібраний editor та export templates містять цей модуль і його новий API.
- Після успішної передачі дерева проєкт не звертається до нього і не звільняє.
- Для `STATUS_FAILED` і `STATUS_CANCELLED` є обробка; старий меш не зникає,
  поки новий не готовий.
- Для чанків застосовуються тільки результати актуальної revision.
- Тіло з `take_static_body()` додане у сцену або вручну звільнене.
- Текстура не перебудовується паралельно з jobs; custom modifiers безпечні для
  worker. Протестуйте shutdown під час активної генерації.
